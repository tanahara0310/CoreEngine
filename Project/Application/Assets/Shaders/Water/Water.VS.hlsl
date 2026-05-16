#include "../../../../Engine/Assets/Shaders/Include/Object/Object3dVertex.hlsli"

// ===== Gerstner Wave 定数バッファ =====
// WaterPlaneObject::BindCustomResources() が b4 にバインドする
struct WaveParams
{
    float2 direction; // 進行方向（XZ, 正規化済み）
    float amplitude; // 振幅
    float wavelength; // 波長
    float speed; // 位相速度
    float steepness; // 横揺れ係数 Q（0=正弦波, 1=完全Gerstner）
    float2 padding;
};

cbuffer WaterConstants : register(b4)
{
    WaveParams gWaves[4]; // 重ね合わせる波（最大 4 本）
    float gTime; // 経過時間（秒）
    float3 gPadding;
};

// ===== フレーム定数バッファ（クリップ平面）=====
// WaterPlaneObject::BindCustomResources() が b5 にバインドする
cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;        // クリップ平面 (A, B, C, D): dot(worldPos, plane) > 0 で描画
    int    gClipEnabled;      // 1 = 有効，0 = 無効
    int    gReflectionEnabled; // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float2 gFramePadding;
};

// ===== 水面専用出力構造体（SV_ClipDistance0 を追加）=====
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
    float  clipDist         : SV_ClipDistance0; // 水面クリップ（反射パス用）
};

/// @brief Gerstner Wave 1 本分の頂点変位を計算する
/// @param worldPos 変位前のワールド座標（XZ を参照、Y を更新）
/// @param wave     波パラメータ
/// @return 変位量（XYZ）
float3 CalcGerstnerOffset(float3 worldPos, WaveParams wave)
{
    // 波数 k = 2π / λ
    float k = 2.0f * 3.14159265f / wave.wavelength;

    // 角周波数 ω ≒ speed * k（簡易近似、本来は √(g*k)）
    float omega = wave.speed * k;

    // 位相 φ = k*(D・P) + ω*t
    float phase = k * dot(wave.direction, worldPos.xz) + omega * gTime;

    float sinP = sin(phase);
    float cosP = cos(phase);

    // Gerstner Wave 変位式
    float3 offset;
    offset.x = wave.steepness * wave.amplitude * wave.direction.x * cosP;
    offset.y = wave.amplitude * sinP;
    offset.z = wave.steepness * wave.amplitude * wave.direction.y * cosP;

    return offset;
}

WaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // ---- 1. ワールド変換 ----
    float4 worldPos4 = mul(input.position, mtx.World);
    float3 worldPos = worldPos4.xyz;

    // ---- 2. Gerstner Wave 頂点変位（ワールド空間） ----
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        totalOffset += CalcGerstnerOffset(worldPos, gWaves[i]);
    }
    worldPos += totalOffset;

    // ---- 3. 変位後の法線・接線を解析的に再計算（Gerstner の偏微分） ----
    float3 normal = float3(0.0f, 1.0f, 0.0f);
    float3 tangent = float3(1.0f, 0.0f, 0.0f);
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        float k = 2.0f * 3.14159265f / gWaves[j].wavelength;
        float omega = gWaves[j].speed * k;
        float phase = k * dot(gWaves[j].direction, worldPos.xz) + omega * gTime;
        float WA = k * gWaves[j].amplitude;
        float sinP = sin(phase);
        float cosP = cos(phase);

        // 法線偏微分
        normal.x -= gWaves[j].direction.x * WA * cosP;
        normal.y -= gWaves[j].steepness * WA * sinP;
        normal.z -= gWaves[j].direction.y * WA * cosP;

        // X 方向接線偏微分
        tangent.x -= gWaves[j].steepness * gWaves[j].direction.x * gWaves[j].direction.x * WA * sinP;
        tangent.y += gWaves[j].direction.x * WA * cosP;
        tangent.z -= gWaves[j].steepness * gWaves[j].direction.x * gWaves[j].direction.y * WA * sinP;
    }
    normal = normalize(normal);
    tangent = normalize(tangent);

    // ---- 4. 出力組み立て ----
    WaterVSOutput output;
    output.texcoord = input.texcoord;

    float4 baseClip = mul(input.position, mtx.WVP);
    float3x3 invWorld3 = transpose((float3x3) mtx.World);
    float3 offsetLS = mul(totalOffset, invWorld3);
    float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = baseClip + offsetClip;

    output.normal = normalize(mul(normal, (float3x3) mtx.WorldInversTranspose));
    output.tangent = normalize(mul(tangent, (float3x3) mtx.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP);

    // ---- 5. クリップ平面（反射パス中に水面自体を除外する） ----
    // gClipEnabled == 0 のときは常に正（全頂点描画）
    output.clipDist = gClipEnabled
        ? dot(float4(worldPos, 1.0f), gClipPlane)
        : 1.0f;

    return output;
}
