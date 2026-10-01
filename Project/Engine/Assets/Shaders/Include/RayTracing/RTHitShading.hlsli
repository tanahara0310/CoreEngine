#ifndef RT_HIT_SHADING_HLSLI
#define RT_HIT_SHADING_HLSLI

// ============================================================
// レイが当たった三角形のヒットシェーディング
// ============================================================

#include "../Lighting/LightStructures.hlsli"
#include "../PBR/PBR.hlsli"
#include "../Common/ColorSpace.hlsli"
// 濡れ暗色化・水面を通った日光の合成（DeferredLighting の水中ライティングと同じ式）
#include "../Lighting/UnderwaterLighting.hlsli"
#include "../../Cloud/Common/CloudShadowCommon.hlsli"

// 表・テクスチャ・ライトは ResourceDescriptorHeap からヒープ内インデックスで引く
// （ルートシグネチャに CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED が要る）
cbuffer RTHitShadingConstants : register(b2)
{
    uint gHitInstanceTableIndex;   // RTHitInstance の表（TLAS のインスタンスと同じ並び）
    uint gHitSubMeshTableIndex;    // RTHitSubMesh の表
    uint gDirectionalLightsIndex;  // DirectionalLightData の配列（0 番がメインライト）
    uint gDirectionalLightCount;   // ディレクショナルライトの数
    uint gSkyIrradianceSHIndex;    // 空の放射照度の SH9（float4 × 9）
    uint gSkyAmbientEnabled;       // 1 = 空の光で照らす
    float gSkyAmbientScale;        // 空の輝度単位 → サーフェス光単位
    uint gHitShadingEnabled;       // 1 = 表がそろっている
    uint gSkySpecularMapIndex;     // 空のスペキュラキューブマップ（5 ミップ）
    uint gSkySpecularEnabled;      // 1 = 空の映り込みを足す
    uint gCloudShadowMapIndex;     // 雲の影のマップ（kRTHitNoTexture = 雲の影なし）
    float gCloudShadowStrength;    // 雲の影の強さ
    // 雲の影のマップが覆う範囲（CloudShadowConstants と同じ値）
    float2 gCloudShadowRegionCenterXZ;
    float gCloudShadowRegionSize;
    float gCloudShadowAnchorY;
    float gCloudShadowEdgeFadeStart;
    float3 gHitShadingPad;
};

// テクスチャを繰り返して引くサンプラ
SamplerState gLinearWrap : register(s1);

static const uint kRTHitNoTexture = 0xFFFFFFFFu;
static const uint kRTHitSubMeshFlagLit = 1u << 0;
static const uint kRTHitSubMeshFlagDither = 1u << 1;

// 頂点の並び（C++ 側 VertexData: 位置 16 + UV 8 + 法線 12 + 接線 12 + 頂点アニメーションの値 16。
// RayTracingSubsystem.cpp の static_assert と一致させる）
static const uint kRTHitVertexStride = 64u;
static const uint kRTHitVertexTexcoordOffset = 16u;
static const uint kRTHitVertexNormalOffset = 24u;

static const float kRTHitSkySpecularMipCount = 5.0f;

/// @brief インスタンス表の 1 行（C++ 側 RTHitInstance と 1:1）
struct RTHitInstance
{
    float4 objectToWorld[3];
    uint vertexBufferIndex;
    uint indexBufferIndex;
    uint firstSubMesh;
    uint subMeshCount;
};

/// @brief サブメッシュ表の 1 行（C++ 側 RTHitSubMesh と 1:1）
struct RTHitSubMesh
{
    uint firstTriangle;
    uint triangleCount;
    uint baseColorTextureIndex;
    uint emissiveTextureIndex;
    float4 baseColor;
    float4 uvTransformU;
    float4 uvTransformV;
    float3 emissive;
    float metallic;
    float roughness;
    float alphaCutoff;
    uint flags;
    uint metallicRoughnessTextureIndex;  // 金属性（B）・粗さ（G）テクスチャ（kRTHitNoTexture = 無し）
};

/// @brief 当たった点の面の情報
struct RTHitSurface
{
    float3 position;   // ワールド座標
    float3 normal;     // 補間した法線（レイが来た側を向く）
    float3 albedo;     // アンリットの材質では出す色そのもの
    float3 emissive;
    float metallic;
    float roughness;
    bool lit;          // false ならアンリット
    bool cutout;       // アルファで抜ける所に当たった
};

/// @brief 頂点位置をワールドへ移す
float3 TransformHitPosition(RTHitInstance instance, float3 position)
{
    return float3(
        dot(instance.objectToWorld[0].xyz, position) + instance.objectToWorld[0].w,
        dot(instance.objectToWorld[1].xyz, position) + instance.objectToWorld[1].w,
        dot(instance.objectToWorld[2].xyz, position) + instance.objectToWorld[2].w);
}

/// @brief 法線をワールドへ移す（3×3 部分の余因子行列を掛ける。長さは正規化しない）
float3 TransformHitNormal(RTHitInstance instance, float3 normal)
{
    const float3 column0 = float3(instance.objectToWorld[0].x, instance.objectToWorld[1].x, instance.objectToWorld[2].x);
    const float3 column1 = float3(instance.objectToWorld[0].y, instance.objectToWorld[1].y, instance.objectToWorld[2].y);
    const float3 column2 = float3(instance.objectToWorld[0].z, instance.objectToWorld[1].z, instance.objectToWorld[2].z);
    return cross(column1, column2) * normal.x + cross(column2, column0) * normal.y + cross(column0, column1) * normal.z;
}

/// @brief テクスチャのミップを、三角形のテクセル密度とレイの広がりから決める
/// @param uvPerMeterLog2 三角形の「UV の長さ / ワールドの長さ」の log2
/// @param footprintLog2  交点でレイが面に落とす幅（m）の log2
float ComputeHitTextureLod(Texture2D<float4> texture, float uvPerMeterLog2, float footprintLog2)
{
    uint width = 1;
    uint height = 1;
    uint levels = 1;
    texture.GetDimensions(0, width, height, levels);
    const float texelsPerUvLog2 = 0.5f * log2(float(width) * float(height));
    return clamp(uvPerMeterLog2 + texelsPerUvLog2 + footprintLog2, 0.0f, float(levels) - 1.0f);
}

/// @brief 当たった三角形の頂点と材質から面の情報を組み立てる
/// @param instanceIndex  InstanceID()（TLAS の並び＝表の並び）
/// @param primitiveIndex PrimitiveIndex()
/// @param barycentrics   交点の重心座標
/// @param rayDirection   レイの向き（正規化済み）
/// @param coneWidth      交点でのレイの広がりの幅（m。テクスチャのミップの選択に使う）
RTHitSurface FetchHitSurface(
    uint instanceIndex, uint primitiveIndex, float2 barycentrics, float3 rayDirection, float coneWidth)
{
    StructuredBuffer<RTHitInstance> instances = ResourceDescriptorHeap[gHitInstanceTableIndex];
    StructuredBuffer<RTHitSubMesh> subMeshes = ResourceDescriptorHeap[gHitSubMeshTableIndex];
    const RTHitInstance instance = instances[instanceIndex];
    ByteAddressBuffer indexBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(instance.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(instance.vertexBufferIndex)];

    const uint3 triangleIndices = indexBuffer.Load3(primitiveIndex * 12u);
    float3 positions[3];
    float2 texcoords[3];
    float3 normals[3];
    [unroll]
    for (uint v = 0; v < 3; ++v)
    {
        const uint vertexOffset = triangleIndices[v] * kRTHitVertexStride;
        positions[v] = TransformHitPosition(instance, asfloat(vertexBuffer.Load3(vertexOffset)));
        texcoords[v] = asfloat(vertexBuffer.Load2(vertexOffset + kRTHitVertexTexcoordOffset));
        normals[v] = asfloat(vertexBuffer.Load3(vertexOffset + kRTHitVertexNormalOffset));
    }
    const float3 weights = float3(1.0f - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);

    // 三角形の番号からサブメッシュ（材質）を探す
    RTHitSubMesh subMesh = subMeshes[instance.firstSubMesh];
    for (uint s = 0; s < instance.subMeshCount; ++s)
    {
        const RTHitSubMesh candidate = subMeshes[instance.firstSubMesh + s];
        if (primitiveIndex - candidate.firstTriangle < candidate.triangleCount)
        {
            subMesh = candidate;
            break;
        }
    }

    RTHitSurface surface;
    surface.position = positions[0] * weights.x + positions[1] * weights.y + positions[2] * weights.z;

    // 面の向きをレイが来た側へそろえ、補間した法線も同じ側へ向ける
    float3 faceNormal = cross(positions[1] - positions[0], positions[2] - positions[0]);
    const float faceArea2 = length(faceNormal);
    faceNormal = (faceArea2 > 1.0e-12f) ? faceNormal / faceArea2 : -rayDirection;
    if (dot(faceNormal, rayDirection) > 0.0f)
    {
        faceNormal = -faceNormal;
    }
    float3 normal = TransformHitNormal(
        instance, normals[0] * weights.x + normals[1] * weights.y + normals[2] * weights.z);
    normal = (dot(normal, normal) > 1.0e-12f) ? normalize(normal) : faceNormal;
    if (dot(normal, faceNormal) < 0.0f)
    {
        normal = -normal;
    }
    surface.normal = normal;

    // 材質の UV 変換を掛けた UV
    float2 uvs[3];
    [unroll]
    for (uint t = 0; t < 3; ++t)
    {
        const float3 uv1 = float3(texcoords[t], 1.0f);
        uvs[t] = float2(dot(subMesh.uvTransformU.xyz, uv1), dot(subMesh.uvTransformV.xyz, uv1));
    }
    const float2 uv = uvs[0] * weights.x + uvs[1] * weights.y + uvs[2] * weights.z;

    // テクセル密度（三角形の UV の面積とワールドの面積の比）と、レイが面に落とす幅
    const float2 edgeUv1 = uvs[1] - uvs[0];
    const float2 edgeUv2 = uvs[2] - uvs[0];
    const float uvArea2 = abs(edgeUv1.x * edgeUv2.y - edgeUv2.x * edgeUv1.y);
    const float uvPerMeterLog2 = 0.5f * log2(max(uvArea2, 1.0e-20f) / max(faceArea2, 1.0e-20f));
    const float footprintLog2 =
        log2(max(coneWidth, 1.0e-6f) / max(abs(dot(faceNormal, rayDirection)), 0.05f));

    float4 baseColor = subMesh.baseColor;
    if (subMesh.baseColorTextureIndex != kRTHitNoTexture)
    {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.baseColorTextureIndex)];
        baseColor *= baseColorTexture.SampleLevel(
            gLinearWrap, uv, ComputeHitTextureLod(baseColorTexture, uvPerMeterLog2, footprintLog2));
    }
    float3 emissive = subMesh.emissive;
    if (subMesh.emissiveTextureIndex != kRTHitNoTexture)
    {
        Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.emissiveTextureIndex)];
        emissive *= emissiveTexture.SampleLevel(
            gLinearWrap, uv, ComputeHitTextureLod(emissiveTexture, uvPerMeterLog2, footprintLog2)).rgb;
    }

    // 金属性・粗さはファクター × テクスチャ（B = 金属性・G = 粗さ。GBuffer.PS と同じ）
    float metallic = subMesh.metallic;
    float roughness = subMesh.roughness;
    if (subMesh.metallicRoughnessTextureIndex != kRTHitNoTexture)
    {
        Texture2D<float4> metallicRoughnessTexture =
            ResourceDescriptorHeap[NonUniformResourceIndex(subMesh.metallicRoughnessTextureIndex)];
        const float4 metallicRoughnessSample = metallicRoughnessTexture.SampleLevel(
            gLinearWrap, uv, ComputeHitTextureLod(metallicRoughnessTexture, uvPerMeterLog2, footprintLog2));
        metallic *= metallicRoughnessSample.b;
        roughness *= metallicRoughnessSample.g;
    }

    const float alphaThreshold = ((subMesh.flags & kRTHitSubMeshFlagDither) != 0) ? 0.5f : subMesh.alphaCutoff;
    surface.cutout = (baseColor.a <= alphaThreshold);
    surface.albedo = saturate(baseColor.rgb);
    surface.emissive = emissive;
    surface.metallic = saturate(metallic);
    surface.roughness = saturate(max(roughness, 0.01f));
    surface.lit = (subMesh.flags & kRTHitSubMeshFlagLit) != 0;
    return surface;
}

/// @brief 光源からの光に掛ける雲の影を返す（DeferredLighting と同じ。1 = 雲の影なし）
/// @param position 照らす点
/// @param toLight  光源の方向（正規化済み）
float SampleHitCloudShadow(float3 position, float3 toLight)
{
    if (gCloudShadowMapIndex == kRTHitNoTexture || gCloudShadowStrength <= 0.0f)
    {
        return 1.0f;
    }
    Texture2D<float> cloudShadowMap = ResourceDescriptorHeap[gCloudShadowMapIndex];
    CloudShadowConstants cloudShadow;
    cloudShadow.regionCenterX = gCloudShadowRegionCenterXZ.x;
    cloudShadow.regionCenterZ = gCloudShadowRegionCenterXZ.y;
    cloudShadow.regionSizeM = gCloudShadowRegionSize;
    cloudShadow.anchorWorldY = gCloudShadowAnchorY;
    cloudShadow.edgeFadeStart = gCloudShadowEdgeFadeStart;
    cloudShadow.sceneStrength = gCloudShadowStrength;
    cloudShadow.pad0 = 0.0f;
    cloudShadow.pad1 = 0.0f;
    return lerp(1.0f, SampleCloudShadow(cloudShadowMap, gLinearWrap, position, toLight, cloudShadow), gCloudShadowStrength);
}

/// @brief 水中の点を照らすときの値（DeferredLighting の水中ライティングと同じ扱い）
struct RTHitUnderwaterLighting
{
    float factor;                 // 0 = 水上 / 1 = 水中（メインライトの直接光を透過光へ置き換える割合）
    float3 ambientTransmittance;  // 空の光と補助ライトに掛ける水の透過率
    float3 transmittedMainLight;  // 水面を通って届くメインライトの放射照度（factor を掛けた値）
};

/// @brief 水上の点の値（水中ライティングを使わない）
RTHitUnderwaterLighting AboveWaterLighting()
{
    RTHitUnderwaterLighting lighting;
    lighting.factor = 0.0f;
    lighting.ambientTransmittance = float3(1.0f, 1.0f, 1.0f);
    lighting.transmittedMainLight = float3(0.0f, 0.0f, 0.0f);
    return lighting;
}

/// @brief 当たった面の放射輝度を返す（DeferredLighting と同じ式。影はメインライトだけ）
/// @param toViewer            面から見る側への向き（レイの向きの逆）
/// @param mainLightVisibility メインライトの日向率（1 = 日向 / 0 = 影）
/// @param underwater          水中の点を照らす値（水上の点は AboveWaterLighting()）
float3 ShadeHitSurface(
    RTHitSurface surface, float3 toViewer, float mainLightVisibility, RTHitUnderwaterLighting underwater)
{
    if (!surface.lit)
    {
        return surface.albedo;
    }

    const float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), surface.albedo, surface.metallic);
    const float3 albedo = ApplyWetDarkening(surface.albedo, underwater.factor);
    const float mainLightShadow = lerp(0.3f, 1.0f, mainLightVisibility);
    float3 color = surface.emissive;
    bool mainLightEnabled = false;
    float mainLightCloudShadow = 1.0f;
    if (gDirectionalLightCount > 0)
    {
        StructuredBuffer<DirectionalLightData> lights = ResourceDescriptorHeap[gDirectionalLightsIndex];
        for (uint i = 0; i < gDirectionalLightCount; ++i)
        {
            const DirectionalLightData light = lights[i];
            if (!light.enabled)
            {
                continue;
            }
            const float3 L = normalize(-light.direction);
            const float shadow = (i == 0) ? mainLightShadow : 1.0f;
            const float cloudShadow = SampleHitCloudShadow(surface.position, L);
            // メインライトの直接光は水中の割合だけ消し、補助ライトは水の透過率で弱める
            const float3 lightScale = (i == 0)
                ? float3(1.0f, 1.0f, 1.0f) * (1.0f - underwater.factor)
                : underwater.ambientTransmittance;
            color += CalculatePBRLighting(
                surface.normal, toViewer, L, light.color.rgb, light.intensity,
                albedo, surface.metallic, surface.roughness, 1.0f) * shadow * cloudShadow * lightScale;
            if (gSkyAmbientEnabled == 0)
            {
                color += CalculateHalfLambertAmbient(
                    surface.normal, L, light.color.rgb, light.intensity, albedo, surface.metallic, 1.0f) * shadow
                    * underwater.ambientTransmittance;
            }
            if (i == 0)
            {
                mainLightEnabled = true;
                mainLightCloudShadow = cloudShadow;
            }
        }
    }

    // 消したメインライトの代わりに、水面を通って届くメインライトで照らす
    if (underwater.factor > 0.0f)
    {
        color += CompositeUnderwaterCaustics(
            underwater.transmittedMainLight, albedo, F0, surface.metallic, mainLightVisibility)
            * mainLightCloudShadow;
    }

    if (gSkyAmbientEnabled != 0)
    {
        const float skyShadow = mainLightEnabled ? mainLightShadow : 1.0f;
        StructuredBuffer<float4> skyIrradianceSH = ResourceDescriptorHeap[gSkyIrradianceSHIndex];
        color += albedo * (1.0f - surface.metallic)
            * EvaluateSkyIrradianceSH9(skyIrradianceSH, surface.normal) * gSkyAmbientScale * skyShadow
            * underwater.ambientTransmittance;

        if (gSkySpecularEnabled != 0)
        {
            TextureCube<float4> skySpecularMap = ResourceDescriptorHeap[gSkySpecularMapIndex];
            const float NdotV = saturate(dot(surface.normal, toViewer));
            const float3 R = reflect(-toViewer, surface.normal);
            const float3 prefiltered = skySpecularMap.SampleLevel(
                gLinearWrap, R, surface.roughness * (kRTHitSkySpecularMipCount - 1.0f)).rgb;
            const float2 envBRDF = EnvBRDFApprox(surface.roughness, NdotV);
            color += prefiltered * (F0 * envBRDF.x + envBRDF.y) * gSkyAmbientScale * skyShadow
                * underwater.ambientTransmittance;
        }
    }
    return color;
}

#endif // RT_HIT_SHADING_HLSLI
