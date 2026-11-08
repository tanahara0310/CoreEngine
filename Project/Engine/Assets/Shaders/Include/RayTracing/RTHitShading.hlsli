#ifndef RT_HIT_SHADING_HLSLI
#define RT_HIT_SHADING_HLSLI

// ============================================================
// レイが当たった三角形のヒットシェーディング
// ============================================================

#include "../Lighting/LightStructures.hlsli"
#include "../PBR/PBR.hlsli"
#include "../Common/ColorSpace.hlsli"
#include "../Common/NormalMap.hlsli"
// 濡れ暗色化・水面を通った日光の合成（DeferredLighting の水中ライティングと同じ式）
#include "../Lighting/ShadowLightFloor.hlsli"
#include "../Lighting/UnderwaterLighting.hlsli"
// 水が引いたあとの濡れ跡（DeferredLighting と同じ式）
#include "../Lighting/WetSurface.hlsli"
#include "../../Cloud/Common/CloudShadowCommon.hlsli"
// 材質の表・頂点・テクスチャの引き方
#include "RTSceneTables.hlsli"

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
    // 濡れ跡（DeferredLighting の gWetSurface と同じ値）
    uint gWetSurfaceEnabled;          // 1 = 濡れ跡を描く
    float gWetSurfaceBottomHeight;    // 表の下端（今の水面の高さ）[m]
    float gWetSurfaceSamplesPerMeter; // 1 m あたりの標本の数
    float4 gWetSurfaceRegion;         // xy = 水面の範囲の中心 / zw = 範囲の半分の幅
    float4 gWetSurfaceTable[kWetSurfaceTableVectorCount]; // 下端から上へ並べた濡れ具合（4 つずつ詰める）
};

float LoadWetSurfaceSample(uint n)
{
    return gWetSurfaceTable[n >> 2][n & 3u];
}

static const float kRTHitSkySpecularMipCount = 5.0f;

/// @brief 当たった点の面の情報
struct RTHitSurface
{
    float3 position;   // ワールド座標
    float3 normal;     // 補間した法線に法線マップを掛けたもの（レイが来た側を向く）
    float3 albedo;     // アンリットの材質では出す色そのもの
    float3 emissive;
    float metallic;
    float roughness;
    float ao;          // 環境光に掛ける AO（1 = 遮蔽なし）
    bool lit;          // false ならアンリット
    bool moving;       // 動く物（スキンメッシュ）。濡れ跡を付けない
};

/// @brief この定数の材質の表（表がそろっていなければ表なし）
RTSceneTables GetHitShadingTables()
{
    if (gHitShadingEnabled == 0)
    {
        return NoRTSceneTables();
    }
    return MakeRTSceneTables(gHitInstanceTableIndex, gHitSubMeshTableIndex);
}

/// @brief 当たった三角形の頂点と材質から面の情報を組み立てる（表がそろっていること）
/// @param hit          当たった三角形
/// @param rayDirection レイの向き（正規化済み）
/// @param coneWidth    交点でのレイの広がりの幅（m。テクスチャのミップの選択に使う）
RTHitSurface FetchHitSurface(RTHitPrimitive hit, float3 rayDirection, float coneWidth)
{
    const RTHitTriangle tri = LoadRTHitTriangle(GetHitShadingTables(), hit, rayDirection);
    const RTHitSubMesh subMesh = tri.subMesh;
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(tri.instance.vertexBufferIndex)];

    float3 normals[3];
    float3 tangents[3];
    [unroll]
    for (uint v = 0; v < 3; ++v)
    {
        normals[v] = asfloat(vertexBuffer.Load3(tri.vertexOffsets[v] + kRTHitVertexNormalOffset));
        tangents[v] = asfloat(vertexBuffer.Load3(tri.vertexOffsets[v] + kRTHitVertexTangentOffset));
    }
    const float3 weights = tri.weights;

    RTHitSurface surface;
    surface.position = tri.positions[0] * weights.x + tri.positions[1] * weights.y + tri.positions[2] * weights.z;

    // 補間した法線を、レイが来た側を向く面の法線と同じ側へ向ける
    float3 normal = TransformHitNormal(
        tri.instance, normals[0] * weights.x + normals[1] * weights.y + normals[2] * weights.z);
    normal = (dot(normal, normal) > 1.0e-12f) ? normalize(normal) : tri.faceNormal;
    if (dot(normal, tri.faceNormal) < 0.0f)
    {
        normal = -normal;
    }
    surface.normal = normal;

    // レイが面に落とす幅（テクスチャのミップの選択に使う）
    const float footprintLog2 = RTHitFootprintLog2(tri, rayDirection, coneWidth);

    const float4 baseColor = SampleRTHitBaseColor(tri, footprintLog2);
    float3 emissive = subMesh.emissive;
    if (subMesh.emissiveTextureIndex != kRTHitNoTexture)
    {
        emissive *= SampleRTHitTexture(subMesh.emissiveTextureIndex, tri, footprintLog2).rgb;
    }

    // 法線マップ（GBuffer.PS と同じ。接線を法線に直交させ、従法線は三角形の UV から画像の上へ向ける）
    if (subMesh.normalTextureIndex != kRTHitNoTexture)
    {
        float3 tangent = TransformHitDirection(
            tri.instance, tangents[0] * weights.x + tangents[1] * weights.y + tangents[2] * weights.z);
        tangent -= dot(tangent, normal) * normal;
        if (dot(tangent, tangent) > 1.0e-12f)
        {
            tangent = normalize(tangent);
            const float bitangentSign = NormalMapBitangentSign(normal, tangent,
                tri.positions[1] - tri.positions[0], tri.positions[2] - tri.positions[0], tri.edgeUv1, tri.edgeUv2);
            const float3 tangentSpaceNormal =
                UnpackNormalMap(SampleRTHitTexture(subMesh.normalTextureIndex, tri, footprintLog2));
            const float3 mappedNormal = normalize(
                tangent * tangentSpaceNormal.x + cross(normal, tangent) * (bitangentSign * tangentSpaceNormal.y)
                + normal * tangentSpaceNormal.z);
            // 面の裏へ回り込んだ法線は使わない
            if (dot(mappedNormal, tri.faceNormal) > 0.0f)
            {
                surface.normal = mappedNormal;
            }
        }
    }

    // 金属性・粗さはファクター × テクスチャ（B = 金属性・G = 粗さ。GBuffer.PS と同じ）
    float metallic = subMesh.metallic;
    float roughness = subMesh.roughness;
    if (subMesh.metallicRoughnessTextureIndex != kRTHitNoTexture)
    {
        const float4 metallicRoughnessSample =
            SampleRTHitTexture(subMesh.metallicRoughnessTextureIndex, tri, footprintLog2);
        metallic *= metallicRoughnessSample.b;
        roughness *= metallicRoughnessSample.g;
    }

    // AO（GBuffer.PS と同じく強さで 1 と混ぜる）
    surface.ao = 1.0f;
    if (subMesh.occlusionTextureIndex != kRTHitNoTexture)
    {
        const float occlusion = SampleRTHitTexture(subMesh.occlusionTextureIndex, tri, footprintLog2).r;
        surface.ao = saturate(lerp(1.0f, occlusion, subMesh.occlusionStrength));
    }

    surface.albedo = saturate(baseColor.rgb);
    surface.emissive = emissive;
    surface.metallic = saturate(metallic);
    surface.roughness = saturate(max(roughness, 0.01f));
    surface.lit = (subMesh.flags & kRTHitSubMeshFlagLit) != 0;
    surface.moving = (subMesh.flags & kRTHitSubMeshFlagMoving) != 0;
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
    cloudShadow.rowOffset = 0u;
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

    // 濡れ跡（水が引いたあとの濡れた面）。動く物には付けず、水中の面の粗さは変えない
    float wetness = 0.0f;
    if (gWetSurfaceEnabled != 0 && !surface.moving)
    {
        wetness = SampleWetSurface(
            surface.position, gWetSurfaceBottomHeight, gWetSurfaceSamplesPerMeter,
            gWetSurfaceRegion.xy, gWetSurfaceRegion.zw);
    }
    const float roughness = ApplyWetRoughness(surface.roughness, wetness * (1.0f - underwater.factor));

    const float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), surface.albedo, surface.metallic);
    const float3 albedo = ApplyWetDarkening(surface.albedo, max(underwater.factor, wetness));
    float3 color = surface.emissive;
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
            const float shadow = (i == 0) ? mainLightVisibility : 1.0f;
            const float cloudShadow = SampleHitCloudShadow(surface.position, L);
            // メインライトの直接光は水中の割合だけ消し、補助ライトは水の透過率で弱める
            const float3 lightScale = (i == 0)
                ? float3(1.0f, 1.0f, 1.0f) * (1.0f - underwater.factor)
                : underwater.ambientTransmittance;
            color += CalculatePBRLighting(
                surface.normal, toViewer, L, light.color.rgb, light.intensity,
                albedo, surface.metallic, roughness, 1.0f) * shadow * cloudShadow * lightScale;
            if (gSkyAmbientEnabled == 0)
            {
                color += CalculateHalfLambertAmbient(
                    surface.normal, L, light.color.rgb, light.intensity, albedo, surface.metallic, surface.ao)
                    * ApplyShadowLightFloor(shadow) * underwater.ambientTransmittance;
            }
            if (i == 0)
            {
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

    // 空の光には太陽の影を掛けない（さえぎりは AO が受け持つ）
    if (gSkyAmbientEnabled != 0)
    {
        StructuredBuffer<float4> skyIrradianceSH = ResourceDescriptorHeap[gSkyIrradianceSHIndex];
        color += albedo * (1.0f - surface.metallic)
            * EvaluateSkyIrradianceSH9(skyIrradianceSH, surface.normal) * gSkyAmbientScale * surface.ao
            * underwater.ambientTransmittance;

        if (gSkySpecularEnabled != 0)
        {
            TextureCube<float4> skySpecularMap = ResourceDescriptorHeap[gSkySpecularMapIndex];
            const float NdotV = saturate(dot(surface.normal, toViewer));
            const float3 R = reflect(-toViewer, surface.normal);
            const float3 prefiltered = skySpecularMap.SampleLevel(
                gLinearWrap, R, roughness * (kRTHitSkySpecularMipCount - 1.0f)).rgb;
            const float2 envBRDF = EnvBRDFApprox(roughness, NdotV);
            color += prefiltered * (F0 * envBRDF.x + envBRDF.y) * gSkyAmbientScale * surface.ao
                * underwater.ambientTransmittance;
        }
    }
    return color;
}

#endif // RT_HIT_SHADING_HLSLI
