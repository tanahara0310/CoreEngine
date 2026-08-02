#include "Object3dVertex.hlsli"
#include "ShaderMath.hlsli" // TWO_PI

// ===== Gerstner Wave 定数バッファ =====
// WaterPlaneObject::BindCustomResources() が b4 にバインドする
struct WaveParams
{
    float2 direction; // 進行方向（XZ, 正規化済み）
    float amplitude; // 振幅
    float wavelength; // 波長
    float speed; // 位相速度
    float steepness; // 横揺れ係数 Q（0=正弦波, 1=完全Gerstner）
    float phaseOffset; // 位相オフセット
    float padding;
};

struct WaveDerivatives
{
    float3 offset;
    float3 dPdX;
    float3 dPdZ;
};

cbuffer WaterConstants : register(b4)
{
    WaveParams gWaves[16]; // 重ね合わせる波（最大 16 本）
    uint gActiveWaveCount; // 実際に評価する波本数
    float gTime; // 経過時間（秒）
    float2 gPadding;
};

// ===== フレーム定数バッファ =====
// WaterPlaneObject::BindCustomResources() が b5 にバインドする。
// VS では実際には使用しないが、PS（Water.PS.hlsl）と同一レイアウトを保つために宣言する。
cbuffer WaterFrameConstants : register(b5)
{
    int    gReflectionEnabled;
    float  gFresnelReflectanceScale;
    float  gFresnelBaseReflectance;

    int    gDepthFadeEnabled;
    int    gDepthFadeDebugEnabled;
    float  gDepthFadeDebugScale;
    float  gSkyAmbientScale;
    int    gSkyAmbientEnabled;

    float3 gAbsorptionCoeff;
    float  gAbsorptionPad;
    float3 gScatteringCoeff;
    float  gScatteringPad;

    uint   gDepthDebugViewMode;
    int    gUseFFTOceanNormalMap;
    float2 gDebugPadding;

    float  gCameraNearZ;
    float  gCameraFarZ;
    float2 gCameraClipPadding;

    // ---- 泡（whitecap）。VS 未使用、PS とのレイアウト一致のため保持 ----
    int    gFoamEnabled;
    float  gFoamBias;
    float  gFoamGain;
    float  gFoamOpacity;
    float3 gFoamCascadeWeights;
    float  gFoamDecaySeconds;
};

// ===== 水面専用出力構造体 =====
struct WaterVSOutput
{
    float4 position         : SV_POSITION;
    float2 texcoord         : TEXCOORD0;
    float3 normal           : NORMAL0;
    float3 worldPosition    : POSITION0;
    float4 lightSpacePos    : POSITION1;
    float3 tangent          : TANGENT0;
    float3 bitangent        : BINORMAL0;
    float4 clipPosCurrent   : POSITION2;
    float4 clipPosPrev      : POSITION3;
};

/// @brief Gerstner Wave 1 本分の頂点変位を計算する
/// @param worldPos 変位前のワールド座標（XZ を参照、Y を更新）
/// @param wave     波パラメータ
/// @return 変位量（XYZ）
WaveDerivatives CalcGerstnerWave(float3 worldPos, WaveParams wave)
{
    // 波数 k = 2π / λ
    float k = TWO_PI / wave.wavelength;

    // 角周波数 ω ≒ speed * k（簡易近似、本来は √(g*k)）
    float omega = wave.speed * k;

    // 極端な尖りと自己交差を避けるため kA に応じて有効 steepness を抑える
    float kA = max(k * wave.amplitude, 1.0e-4f);
    float safeSteepness = min(wave.steepness, 0.95f / kA);

    // 位相 φ = k*(D・P) + ω*t
    float phase = k * dot(wave.direction, worldPos.xz) + omega * gTime + wave.phaseOffset;

    float sinP = sin(phase);
    float cosP = cos(phase);

    // Gerstner Wave 変位式
    WaveDerivatives result;
    result.offset.x = safeSteepness * wave.amplitude * wave.direction.x * cosP;
    result.offset.y = wave.amplitude * sinP;
    result.offset.z = safeSteepness * wave.amplitude * wave.direction.y * cosP;

    float common = safeSteepness * wave.amplitude * k * sinP;
    float heightSlope = wave.amplitude * k * cosP;

    result.dPdX = float3(
        1.0f - common * wave.direction.x * wave.direction.x,
        heightSlope * wave.direction.x,
       -common * wave.direction.x * wave.direction.y);

    result.dPdZ = float3(
       -common * wave.direction.x * wave.direction.y,
        heightSlope * wave.direction.y,
        1.0f - common * wave.direction.y * wave.direction.y);

    return result;
}

WaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // ---- 1. ワールド変換 ----
    float4 worldPos4 = mul(input.position, mtx.World);
    float3 worldPos = worldPos4.xyz;
    // 法線・接線の解析偏微分は変位前のワールド静止位置で評価する
    float3 restPos = worldPos;

    // ---- 2. Gerstner Wave 頂点変位（ワールド空間） ----
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        if (i >= gActiveWaveCount)
        {
            break;
        }

        WaveDerivatives wave = CalcGerstnerWave(restPos, gWaves[i]);
        totalOffset += wave.offset;
        dPdX += wave.dPdX - float3(1.0f, 0.0f, 0.0f);
        dPdZ += wave.dPdZ - float3(0.0f, 0.0f, 1.0f);
    }

    worldPos += totalOffset;

    // ---- 3. 解析偏微分から接線空間を再構築する ----
    float3 tangent = normalize(dPdX);
    float3 binormal = normalize(dPdZ);
    float3 normal = normalize(cross(binormal, tangent));

    if (normal.y < 0.0f)
    {
        normal = -normal;
    }

    // ---- 4. 出力組み立て ----
    WaterVSOutput output;
    output.texcoord = input.texcoord;

    float4 baseClip = mul(input.position, mtx.WVP);
    // WorldInversTranspose = (World^-1)^T なので transpose すると World^-1 になる
    // スケールが変わっても変位量が増幅されないように正しい逆行列を使用する
    float3x3 invWorld3 = transpose((float3x3) mtx.WorldInversTranspose);
    float3 offsetLS = mul(totalOffset, invWorld3);
    float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = baseClip + offsetClip;

    // dPdX/dPdZ から再構成した法線・接線はすでにワールド空間なので
    // ここで再度 World / WorldInversTranspose を掛けるとスケール依存の歪みが発生する。
    output.normal = normal;
    output.tangent = tangent;
    output.bitangent = binormal;

    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP);

    return output;
}
