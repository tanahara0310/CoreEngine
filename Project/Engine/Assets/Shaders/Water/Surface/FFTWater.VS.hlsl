#include "Object3dVertex.hlsli"
#include "../Common/WaveSurface.hlsli"

// カスケード（マルチスケールFFT）は Texture2DArray のスライスに格納される。
// ヤコビアン（t20）は頂点解像度に依存しないよう Water.PS 側で直接サンプルする。
Texture2DArray<float4> gFFTOceanDisplacement : register(t18);
SamplerState gLinearClamp : register(s2);
// WRAP サンプラ（カスケードのワールドタイリング用）。gSampler は Anisotropic=WRAP。
SamplerState gSampler : register(s0);

// 波群エンベロープの位相（gWaveGroupPhase）を読む。レイアウトは Water.PS.hlsl と同じ。
#include "../Common/WaterFrameConstants.hlsli"
#include "../Common/WaterShoreMesh.hlsli"
#include "../Common/WaterSeaMap.hlsli"
#include "../Common/WaterRippleGrid.hlsli"
#include "../Common/WaterShoreRunup.hlsli"

// 海の範囲マップ（水深と上の物までの高さ。gSeaMapEnabled = 0 のときは読まない）
Texture2D<float4> gWaterSeaMap : register(t29);
// 水の波紋の面（gRippleEnabled = 0 のときは読まない。Water.PS と同じレジスタ）
Texture2DArray<float4> gWaterRipple : register(t30);
// 岸の格子の頂点の値（WaterShoreMeshDisplacement.CS。gShoreMeshEnabled = 0 のときは読まない）
// 変位（xyz）と物が起こした波紋の高さ（w）
Texture2D<float4> gWaterShoreMeshDisplacement : register(t35);
// 沖から寄せる波の傾き（xy）と砕けた波の泡（zw）
Texture2D<float4> gWaterShoreMeshSurface : register(t36);

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
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
    // 1 = 岸の細かい格子の頂点（Water.PS は粗い格子の範囲の中の画素を描かない）
    nointerpolation float shoreMeshVertex : TEXCOORD3;
    // 沖から寄せる波の傾き（∂高さ/∂x, ∂高さ/∂z。Water.PS が FFT の傾きへ足す）
    float2 shoreWaveSlope : TEXCOORD4;
    // 砕けた波の泡（x = 被覆率 [0,1]・y = 段と浜を上がる水の泡を描く度合い [0,1]。Water.PS が岸の泡へ重ねる）
    float2 shoreBreakFoam : TEXCOORD5;
};

/// @brief 海の範囲マップから、頂点の変位に掛ける倍率と沖から寄せる波を返す
/// @details 倍率は RTWaterSurfaceCommon.hlsli の EvaluateSurfaceSeaMapWaveScale、寄せる波は
///          SampleMeshVertexWaveDisplacement と同じ式
/// @param vertexSpacing 頂点の格子の間隔 [m]（地図のテクセルより広ければ粗い格子用の値を使う）
/// @param shoreWave 沖から寄せる波の (高さ [m], ∂高さ/∂x, ∂高さ/∂z)
/// @param crestFoam 砕ける直前から峰の頂に出る泡の被覆率
float GetSeaMapWaveScale(float2 baseXZ, float vertexSpacing, out float3 shoreWave, out float crestFoam)
{
    shoreWave = float3(0.0f, 0.0f, 0.0f);
    crestFoam = 0.0f;
    if (gSeaMapEnabled == 0)
    {
        return 1.0f;
    }
    const float4 value = SampleWaterSeaMap(
        gWaterSeaMap, baseXZ, gSeaMapOriginXZ, gSeaMapTexelsPerMeter, gSeaMapResolution);
    const bool conservative = UseConservativeWaterSeaMap(vertexSpacing, gSeaMapTexelsPerMeter);
    if (gShoreWaveHeight > 0.0f && !IsWaterSeaMapBasin(value.x))
    {
        const float4 field = SampleShoreWaveField(
            gWaterSeaMap, baseXZ, gSeaMapOriginXZ, gSeaMapTexelsPerMeter, gSeaMapResolution);
        const float clearance = (conservative ? value.w : value.y) - gSeaMapDepthOffset;
        shoreWave = EvaluateShoreWave(
            value.x + gSeaMapDepthOffset, clearance, field, gShoreRunupTime, gShoreWaveHeight, vertexSpacing,
            baseXZ, crestFoam);
    }
    return ComputeWaterSeaMapWaveScale(value, conservative, gSeaMapDepthOffset, gSeaMapInvWaveHeight);
}

/// @brief 頂点が描かれる位置の波紋の高さ [m]（RTWaterSurfaceCommon.hlsli の EvaluateSurfaceRippleVertexHeight と同じ式）
/// @param vertexSpacing 頂点の格子の間隔 [m]（ComputeRippleGeometryLevelWeight）
float GetRippleVertexHeight(float2 drawnXZ, float vertexSpacing)
{
    if (gRippleEnabled == 0)
    {
        return 0.0f;
    }
    return EvaluateRippleVertexHeight(
        gWaterRipple, drawnXZ, vertexSpacing, gRippleOriginsXZ01, gRippleOrigin2XZ, gRippleInvSizes, gRippleStrength);
}

/// @brief 頂点の変位（格子の間隔で解像できるカスケード＋波群エンベロープ＋海の範囲マップ＋沖から寄せる波＋
///        描かれる位置の波紋の高さ）
/// @param vertexSpacing 頂点の格子の間隔 [m]（ComputeFFTGeometryCascadeWeight）
/// @param shoreWaveSlope 沖から寄せる波の傾き（∂高さ/∂x, ∂高さ/∂z）
/// @param crestFoam      砕ける直前から峰の頂に出る泡の被覆率
float3 SampleVertexDisplacement(float2 baseXZ, float vertexSpacing, out float2 shoreWaveSlope, out float crestFoam)
{
    float3 shoreWave;
    float3 displacement = SampleFFTVertexDisplacement(gFFTOceanDisplacement, gSampler, baseXZ, vertexSpacing, gWaveGroupPhase)
        * GetSeaMapWaveScale(baseXZ, vertexSpacing, shoreWave, crestFoam);
    displacement.y += shoreWave.x;
    shoreWaveSlope = shoreWave.yz;
    displacement.y += GetRippleVertexHeight(baseXZ + displacement.xz, vertexSpacing);
    return displacement;
}

/// @brief 粗い格子の頂点の間隔 [m]
float GetCoarseMeshVertexSpacing()
{
    return max(gMeshCellSizeXZ.x, gMeshCellSizeXZ.y);
}

/// @brief 岸の格子の頂点の変位（WaterShoreMeshDisplacement.CS が頂点ごとに書いた値。範囲の端ほど粗い格子の形へ寄せてある）
/// @param gridUV         岸の格子の中の頂点の位置（animData.xy。0..1）
/// @param shoreWaveSlope 沖から寄せる波の傾き（岸の格子の頂点の値）
/// @param shoreBreakFoam x = 砕けた波の泡の被覆率（峰の頂の泡と、段と浜を上がる水の泡の大きい方）、
///                       y = 段と浜を上がる水の泡を描く度合い（沖から寄せる波と浜を駆け上がる水があるとき、範囲の中で 1・端ほど 0）
float3 LoadShoreMeshVertexDisplacement(float2 gridUV, out float2 shoreWaveSlope, out float2 shoreBreakFoam)
{
    uint width;
    uint height;
    gWaterShoreMeshDisplacement.GetDimensions(width, height);
    const int2 texel = int2(round(gridUV * float2(width - 1u, height - 1u)));
    const float4 surface = gWaterShoreMeshSurface.Load(int3(texel, 0));
    shoreWaveSlope = surface.xy;
    shoreBreakFoam = surface.zw;
    return gWaterShoreMeshDisplacement.Load(int3(texel, 0)).xyz;
}

FFTWaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];
    float3x3 invWorld3 = transpose((float3x3)mtx.WorldInversTranspose);

    // 岸の格子の頂点は、範囲の中の位置を水面メッシュの範囲へ寄せて静止水面の高さへ置く（範囲が無いときは描かない）
    const bool shoreMeshVertex = IsShoreMeshVertex(input.animData);
    float4 localPosition = input.position;
    float3 baseWorldPos;
    if (shoreMeshVertex)
    {
        if (gShoreMeshEnabled == 0)
        {
            FFTWaterVSOutput culled = (FFTWaterVSOutput)0;
            culled.position = float4(0.0f, 0.0f, -1.0f, 1.0f);
            return culled;
        }
        const float3 meshOrigin = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), mtx.World).xyz;
        const float2 baseXZ = ClampShoreMeshVertexToSurface(
            gSeabedOriginXZ + input.animData.xy / gSeabedInvSize, gMeshGridOriginXZ, gMeshGridOriginXZ + gMeshGridSizeXZ);
        baseWorldPos = float3(baseXZ.x, meshOrigin.y, baseXZ.y);
        localPosition = float4(mul(baseWorldPos - meshOrigin, invWorld3), 1.0f);
    }
    else
    {
        baseWorldPos = mul(input.position, mtx.World).xyz;
    }

    float2 shoreWaveSlope;
    float2 shoreBreakFoam = float2(0.0f, 0.0f);
    float3 displacement;
    if (shoreMeshVertex)
    {
        displacement = LoadShoreMeshVertexDisplacement(input.animData.xy, shoreWaveSlope, shoreBreakFoam);
    }
    else
    {
        float crestFoam;
        displacement = SampleVertexDisplacement(baseWorldPos.xz, GetCoarseMeshVertexSpacing(), shoreWaveSlope, crestFoam);
        shoreBreakFoam.x = crestFoam;
    }

    // 潮の分だけ水面全体を上下させる（静止水面からの波の高さ waveHeight には含めない）
    const float3 tideOffset = float3(0.0f, gTideHeight, 0.0f);
    float3 worldPos = baseWorldPos + displacement + tideOffset;

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

    float4 baseClip = mul(localPosition, mtx.WVP);
    float3 offsetLS = mul(displacement + tideOffset, invWorld3);
    float4 offsetClip = mul(float4(offsetLS, 0.0f), mtx.WVP);
    output.position = baseClip + offsetClip;

    output.normal = normalize(mul(baseNormalLocal, (float3x3)mtx.WorldInversTranspose));
    output.tangent = normalize(mul(baseTangentLocal, (float3x3)mtx.World));
    output.bitangent = normalize(mul(baseBinormalLocal, (float3x3)mtx.World));
    output.worldPosition = worldPos;
    output.clipPosCurrent = output.position;
    // ★前フレーム位置にも同じ変位を載せる★
    // 変位を載せないと「今フレームだけ波の分だけ動いた」ことになり、モーションベクターが
    // 波高ぶんの偽の動きを含む。TAA はそれで履歴を引くので水面が常にぶれる。
    // ここでは現フレームの変位を流用している（前フレームの変位テクスチャは保持していない）。
    // カメラ運動は厳密に正しくなり、残るのは波自身の 1 フレーム分の動き（軌道速度 ~1m/s ＝
    // 16ms で 0.02m）だけで、これはサブピクセルなので無視できる。
    float4 basePrevClip = mul(localPosition, mtx.PrevWVP);
    float4 offsetPrevClip = mul(float4(offsetLS, 0.0f), mtx.PrevWVP);
    output.clipPosPrev = basePrevClip + offsetPrevClip;
    output.shoreMeshVertex = shoreMeshVertex ? 1.0f : 0.0f;
    output.shoreWaveSlope = shoreWaveSlope;
    output.shoreBreakFoam = shoreBreakFoam;
    return output;
}
