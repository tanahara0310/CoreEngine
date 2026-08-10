#include "Object3dVertex.hlsli"
// Gerstner 波の数式の唯一の情報源（RTWaterSurfaceCommon / WaterCaustics.PS と共有）。
// 以前ここに独自の CalcGerstnerWave があり「基準不一致」型バグの温床だった。
#include "../Common/GerstnerWave.hlsli"

// ===== Gerstner Wave 定数バッファ =====
// WaterPlaneObject::BindCustomResources() が b4 にバインドする。
// C++ 側の対応構造体: WaterConstants（WaterSurfaceTypes.h）
cbuffer WaterConstants : register(b4)
{
    GerstnerWave gWaves[GERSTNER_MAX_WAVE_COUNT]; // 重ね合わせる波（最大 16 本）
    uint gActiveWaveCount; // 実際に評価する波本数
    float gTime; // 経過時間（秒）
    float2 gPadding;
};

// ===== フレーム定数バッファ =====
// VS では実際には使用しないが、PS（Water.PS.hlsl）と同一レイアウトを保つために宣言する。
#include "../Common/WaterFrameConstants.hlsli"

// ===== 水面専用出力構造体 =====
struct WaterVSOutput
{
    float4 position         : SV_POSITION;
    float2 texcoord         : TEXCOORD0;
    // 変位前の静止ワールドXZ。FFT 経路（FFTWater.VS）と PS の入力レイアウトを
    // 揃えるために Gerstner 経路でも出力する（Gerstner は FFT テクスチャを
    // 引かないので値自体は法線計算に使われない）。
    float2 baseWorldXZ      : TEXCOORD1;
    float  waveHeight       : TEXCOORD2; ///< 静止水面からの波の高さ [m]
    float3 normal           : NORMAL0;
    float3 worldPosition    : POSITION0;
    float4 lightSpacePos    : POSITION1;
    float3 tangent          : TANGENT0;
    float3 bitangent        : BINORMAL0;
    float4 clipPosCurrent   : POSITION2;
    float4 clipPosPrev      : POSITION3;
};

WaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // ---- 1. ワールド変換 ----
    float4 worldPos4 = mul(input.position, mtx.World);
    float3 worldPos = worldPos4.xyz;
    // 法線・接線の解析偏微分は変位前のワールド静止位置で評価する
    float3 restPos = worldPos;

    // ---- 2. Gerstner Wave 頂点変位（ワールド空間） ----
    // 数式は GerstnerWave.hlsli（RT・コースティクスと共有）を必ず経由する
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);
    [unroll]
    for (int i = 0; i < GERSTNER_MAX_WAVE_COUNT; ++i)
    {
        if (i >= gActiveWaveCount)
        {
            break;
        }

        totalOffset += EvaluateGerstnerWaveOffset(gWaves[i], gTime, restPos.xz);
        AccumulateGerstnerWaveDerivatives(gWaves[i], gTime, restPos.xz, dPdX, dPdZ);
    }

    worldPos += totalOffset;

    // ---- 3. 解析偏微分から接線空間を再構築する ----
    float3 tangent = normalize(dPdX);
    float3 binormal = normalize(dPdZ);
    float3 normal = BuildGerstnerNormal(dPdX, dPdZ);

    // ---- 4. 出力組み立て ----
    WaterVSOutput output;
    output.texcoord = input.texcoord;
    // 変位前の静止位置（Gerstner の偏微分評価に使っているものと同じ）
    output.baseWorldXZ = restPos.xz;
    output.waveHeight = totalOffset.y;

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
    // 前フレーム位置にも同じ変位を載せる（理由は FFTWater.VS.hlsl の同じ箇所を参照）
    float4 basePrevClip = mul(input.position, mtx.PrevWVP);
    float4 offsetPrevClip = mul(float4(offsetLS, 0.0f), mtx.PrevWVP);
    output.clipPosPrev = basePrevClip + offsetPrevClip;

    return output;
}
