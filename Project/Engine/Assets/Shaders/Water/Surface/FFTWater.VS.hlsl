#include "Object3dVertex.hlsli"

Texture2D<float4> gFFTOceanDisplacement : register(t18);
Texture2D<float4> gFFTOceanJacobian : register(t20);
SamplerState gLinearClamp : register(s2);

cbuffer WaterFrameConstants : register(b5)
{
    float4 gClipPlane;
    int gClipEnabled;
    int gReflectionEnabled;
    float gFresnelReflectanceScale;
    float gFresnelBaseReflectance;
    int gDepthFadeEnabled;
    int gDepthFadeDebugEnabled;
    float gDepthFadeDebugScale;
    float gSkyAmbientScale;
    float3 gAbsorptionCoeff;
    int gSkyAmbientEnabled;
    float3 gScatteringCoeff;
    float gScatteringPad;
    uint gDepthDebugViewMode;
    int gUseFFTOceanNormalMap;
    float2 gDebugPadding;
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
    float clipDist : SV_ClipDistance0;
};

FFTWaterVSOutput main(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    float4 worldPos4 = mul(input.position, mtx.World);
    float3 baseWorldPos = worldPos4.xyz;
    float scaleX = length(mtx.World[0].xyz);
    float scaleZ = length(mtx.World[2].xyz);
    float2 scaleUV = float2(max(scaleX, 1.0e-4f), max(scaleZ, 1.0e-4f));

    // メッシュの texcoord は PlaneMeshGenerator が { u, 1 - v }（V 反転）で生成するため、
    // そのままサンプリングするとテクスチャ +v 方向がワールド -Z に対応してしまう。
    // 変位はジオメトリと常に同時サンプリングなので見た目は破綻しないが、
    // 法線マップ（FFTOceanFinalize.CS が +v = +Z 前提で ∂h/∂v から生成）を
    // ピクセルシェーダーで (0,0,1) ビタンジェントへ展開した瞬間に
    // 「シェーディング法線の Z 成分だけ実ジオメトリと符号が逆」になり、
    // フレネル・反射の明るいまだらが実際の波の斜面と無関係な位置に現れる。
    // また choppiness の水平変位 Z も高さ場に対して鏡写しに適用されていた。
    // ここで V を戻して「テクスチャ +v = ワールド +Z」に統一する。
    float2 uvLocal = float2(input.texcoord.x, 1.0f - input.texcoord.y);
    float2 sampleUV = frac(uvLocal * scaleUV);

    float3 displacement = gFFTOceanDisplacement.SampleLevel(gLinearClamp, sampleUV, 0.0f).xyz;

    float3 worldPos = baseWorldPos + displacement;

    // タンジェント基底は波の傾き（fftNormal）に依存させず、水面が平坦なときの基準フレームを
    // そのままワールド変換して渡す。法線自体はピクセルシェーダー側で gFFTOceanNormal を
    // texcoord から直接再サンプリングして構築するため（頂点解像度に依存しない滑らかな法線を得るため）、
    // ここでは TBN 基底の元になるローカル軸のみを用意すればよい。
    const float3 baseNormalLocal = float3(0.0f, 1.0f, 0.0f);
    const float3 baseTangentLocal = float3(1.0f, 0.0f, 0.0f);
    const float3 baseBinormalLocal = float3(0.0f, 0.0f, 1.0f);

    FFTWaterVSOutput output;
    // ピクセルシェーダーの法線マップ再サンプリングが、ここでサンプリングした変位と
    // 同じ空間スケール・同じ軸向きになるよう、V 反転を戻しスケール適用後の UV を渡す
    // （frac はPS側でピクセル単位に行う）。
    // 以前は素の texcoord を渡していたため、メッシュがスケールされていると
    // シェーディング法線が実際の波形とズレ、ジオメトリと無関係な位置に
    // フレネル起因の明るいまだらが出ていた。
    output.texcoord = uvLocal * scaleUV;
    output.jacobianData = gFFTOceanJacobian.SampleLevel(gLinearClamp, sampleUV, 0.0f);

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
    output.clipDist = gClipEnabled ? dot(float4(worldPos, 1.0f), gClipPlane) : 1.0f;
    return output;
}
