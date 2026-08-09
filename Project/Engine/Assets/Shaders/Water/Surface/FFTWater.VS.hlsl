#include "Object3dVertex.hlsli"
// カスケード定数・回転写像・波群エンベロープの唯一の情報源
#include "../Common/FFTOceanCascade.hlsli"

// カスケード（マルチスケールFFT）は Texture2DArray のスライスに格納される。
// ヤコビアン（t20）は頂点解像度に依存しないよう Water.PS 側で直接サンプルする
// （以前ここでカスケード0のみを頂点サンプルして渡していた経路は廃止）。
Texture2DArray<float4> gFFTOceanDisplacement : register(t18);
SamplerState gLinearClamp : register(s2);
// WRAP サンプラ（カスケードのワールドタイリング用）。gSampler は Anisotropic=WRAP。
SamplerState gSampler : register(s0);

// VS では実際には使用しないが、PS（Water.PS.hlsl）と同一レイアウトを保つために宣言する。
#include "../Common/WaterFrameConstants.hlsli"

struct FFTWaterVSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    // 変位を加える「前」の参照格子ワールドXZ（＝FFT テクスチャの引数 x0）。
    // FFT の変位・法線・ヤコビアンはすべて x0 の関数であり、描画点
    // x = x0 + D(x0) で引いてはいけない（水平変位ぶんズレる）。
    // PS 側は worldPosition.xz ではなく必ずこの値でカスケードをサンプルする。
    float2 baseWorldXZ : TEXCOORD1;
    // 静止水面からの波の高さ [m]（＝頂点変位の鉛直成分）。
    // 波峰のサブサーフェス透過が「どこが薄い水か」を知るために使う。
    // cbuffer を増やさずに済むよう補間値で運ぶ（VS が正確な値を持っている）。
    float waveHeight : TEXCOORD2;
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
        // ワールドXZ を回転格子系へ（uv = R(θ)·p / patch）
        float2 cuv = ComputeFFTCascadeUV(baseWorldPos.xz, ci);
        float3 d = gFFTOceanDisplacement.SampleLevel(gSampler, float3(cuv, (float)ci), 0.0f).xyz;
        // テクスチャ格子系の水平変位 (d.x, d.z) をワールドへ逆回転（高さ d.y はそのまま）
        float2 horizontal = RotateFromFFTCascadeGrid(float2(d.x, d.z), ci);
        displacement += float3(horizontal.x, d.y, horizontal.y);
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
    // 変位前の参照格子座標。上の displacement サンプリングと同一の引数であること。
    output.baseWorldXZ = baseWorldPos.xz;
    // 波群エンベロープ適用後の鉛直変位＝静止水面からの高さ
    output.waveHeight = displacement.y;

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
    // ★前フレーム位置にも同じ変位を載せる★
    // 変位を載せないと「今フレームだけ波の分だけ動いた」ことになり、モーションベクターが
    // 波高ぶんの偽の動きを含む。TAA はそれで履歴を引くので水面が常にぶれる。
    // ここでは現フレームの変位を流用している（前フレームの変位テクスチャは保持していない）。
    // カメラ運動は厳密に正しくなり、残るのは波自身の 1 フレーム分の動き（軌道速度 ~1m/s ＝
    // 16ms で 0.02m）だけで、これはサブピクセルなので無視できる。
    float4 basePrevClip = mul(input.position, mtx.PrevWVP);
    float4 offsetPrevClip = mul(float4(offsetLS, 0.0f), mtx.PrevWVP);
    output.clipPosPrev = basePrevClip + offsetPrevClip;
    return output;
}
