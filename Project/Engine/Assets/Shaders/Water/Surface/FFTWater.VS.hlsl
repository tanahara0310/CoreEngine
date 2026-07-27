#include "Object3dVertex.hlsli"

// カスケード（マルチスケールFFT）は Texture2DArray のスライスに格納される。
Texture2DArray<float4> gFFTOceanDisplacement : register(t18);
Texture2DArray<float4> gFFTOceanJacobian : register(t20);
SamplerState gLinearClamp : register(s2);
// WRAP サンプラ（カスケードのワールドタイリング用）。gSampler は Anisotropic=WRAP。
SamplerState gSampler : register(s0);

// マルチスケール・カスケード。異なるパッチ長の独立FFTを合算し、単一タイルの
// 「格子状の繰り返し」を打ち消す。値は FFTOceanManager の kCascadePatchLength と一致必須。
// パッチ長は互いに素（全て素数）で合成周期を事実上無限化し、さらにカスケードごとに
// サンプリング格子を回転（0°/+26°/-49°）して残存周期の空間整列も破壊する。
// 回転定数は FFTOceanManager kCascadeRotCos/Sin および Water.PS / RTWaterSurfaceCommon と一致必須。
static const int kFFTCascadeCount = 3;
static const float kFFTCascadePatch[3] = { 521.0f, 127.0f, 31.0f };
static const float kFFTCascadeRotC[3] = { 1.0f, 0.89879405f, 0.65605903f };
static const float kFFTCascadeRotS[3] = { 0.0f, 0.43837115f, -0.75471006f };

// ===== 波群エンベロープ（タイル周期破壊の空間振幅変調）=====
// カスケード0（波高を支配する最大パッチ）は自分自身のタイルが 521m 周期で
// 正確に繰り返すため、「同じ波の高低差が一定間隔で並ぶ」列がどうしても残る。
// 無理数比の方向・波長（≈364/377/707m）の正弦和による滑らかな包絡を波高に掛けると、
// どのカスケード格子とも整列しない非周期変調になり、同一コピーが二度と同じ強さで
// 現れなくなる（実海面の波群＝セット波の見た目にも一致）。
// Water.PS / RTWaterSurfaceCommon と完全一致必須。
static const float kFFTWaveGroupStrength = 0.12f;
float ComputeFFTWaveGroupEnvelope(float2 worldXZ)
{
    float g = sin(dot(worldXZ, float2(0.01071f, 0.01353f)) + 0.917f)
            + sin(dot(worldXZ, float2(-0.01409f, 0.00893f)) + 2.618f)
            + sin(dot(worldXZ, float2(0.00531f, -0.00713f)) + 4.523f);
    return 1.0f + kFFTWaveGroupStrength * g;
}
// 頂点変位（ジオメトリ）に使うカスケード数。最小パッチ（23m のさざ波）は
// メッシュ頂点間隔（≈15.6m）より波長が短くジオメトリでは解像できないため、
// 変位には含めず PS の法線ディテール（Water.PS の全カスケード合算）だけで表現する。
// 含めると頂点ごとに位相が飛んだエイリアシングノイズ（頂点のちらつき）になる。
static const int kFFTGeometryCascadeCount = 2;

// VS では実際には使用しないが、PS（Water.PS.hlsl）と同一レイアウトを保つために宣言する。
cbuffer WaterFrameConstants : register(b5)
{
    int gReflectionEnabled;
    float gFresnelReflectanceScale;
    float gFresnelBaseReflectance;
    int gDepthFadeEnabled;
    int gDepthFadeDebugEnabled;
    float gDepthFadeDebugScale;
    float gSkyAmbientScale;
    int gSkyAmbientEnabled;
    float3 gAbsorptionCoeff;
    float gAbsorptionPad;
    float3 gScatteringCoeff;
    float gScatteringPad;
    uint gDepthDebugViewMode;
    int gUseFFTOceanNormalMap;
    float2 gDebugPadding;
    // 描画カメラのクリップ距離（PS と共有、VS では未使用、レイアウト一致のため保持）
    float gCameraNearZ;
    float gCameraFarZ;
    float2 gCameraClipPadding;
};

struct FFTWaterVSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 jacobianData : TEXCOORD1;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

FFTWaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    float4 worldPos4 = mul(input.position, mtx.World);
    float3 baseWorldPos = worldPos4.xyz;

    // 各カスケードを「ワールドXZ / パッチ長」でサンプルして合算する。
    // メッシュのスケール・UVタイリングには一切依存しない（旧 frac(uvLocal*scaleUV) の
    // 単一タイル反復＝格子状の繰り返しを廃止）。WRAP サンプラで各カスケードのワールド
    // タイルを繰り返すが、パッチ長が非整数比なので合算結果は視界内で反復しない。
    // FFTOceanFinalize.CS はワールド軸（+X=texU, +Z=texV, Y=up）でエンコードするため、
    // ワールドXZをそのまま UV にすれば法線マップとも軸・スケールが一致する。
    float3 displacement = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int ci = 0; ci < kFFTGeometryCascadeCount; ++ci)
    {
        const float rc = kFFTCascadeRotC[ci];
        const float rs = kFFTCascadeRotS[ci];
        // ワールドXZ を回転格子系へ（uv = R(θ)·p / patch）
        float2 cuv = float2(
            rc * baseWorldPos.x - rs * baseWorldPos.z,
            rs * baseWorldPos.x + rc * baseWorldPos.z) / kFFTCascadePatch[ci];
        float3 d = gFFTOceanDisplacement.SampleLevel(gSampler, float3(cuv, (float)ci), 0.0f).xyz;
        // テクスチャ格子系の水平変位 (d.x, d.z) をワールドへ逆回転（高さ d.y はそのまま）
        displacement += float3(rc * d.x + rs * d.z, d.y, -rs * d.x + rc * d.z);
    }

    // 波群エンベロープでタイル周期を崩す（PS の法線・RT の波面評価と同一の変調）
    displacement *= ComputeFFTWaveGroupEnvelope(baseWorldPos.xz);

    float3 worldPos = baseWorldPos + displacement;

    // タンジェント基底は波の傾き（fftNormal）に依存させず、水面が平坦なときの基準フレームを
    // そのままワールド変換して渡す。法線自体はピクセルシェーダー側で gFFTOceanNormal を
    // texcoord から直接再サンプリングして構築するため（頂点解像度に依存しない滑らかな法線を得るため）、
    // ここでは TBN 基底の元になるローカル軸のみを用意すればよい。
    const float3 baseNormalLocal = float3(0.0f, 1.0f, 0.0f);
    const float3 baseTangentLocal = float3(1.0f, 0.0f, 0.0f);
    const float3 baseBinormalLocal = float3(0.0f, 0.0f, 1.0f);

    FFTWaterVSOutput output;
    // PS 側はワールドXZから各カスケードの法線を直接再サンプルするため、texcoord は
    // FFT サンプリングには使わない（生の UV をそのまま渡すのみ）。
    output.texcoord = input.texcoord;
    // ヤコビアン（フォーム/砕波の可視化）は最大カスケード（低周波の大波）を代表として使う。
    output.jacobianData = gFFTOceanJacobian.SampleLevel(gSampler, float3(baseWorldPos.xz / kFFTCascadePatch[0], 0.0f), 0.0f);

    float4 baseClip = mul(input.position, mtx.WVP);
    float3x3 invWorld3 = transpose((float3x3)mtx.WorldInversTranspose);
    float3 offsetLS = mul(displacement, invWorld3);
    float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = baseClip + offsetClip;

    output.normal = normalize(mul(baseNormalLocal, (float3x3)mtx.WorldInversTranspose));
    output.tangent = normalize(mul(baseTangentLocal, (float3x3)mtx.World));
    output.bitangent = normalize(mul(baseBinormalLocal, (float3x3)mtx.World));
    output.worldPosition = worldPos;
    output.lightSpacePos = mul(float4(worldPos, 1.0f), mtx.LightViewProjection);
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(input.position, mtx.PrevWVP);
    return output;
}
