#ifndef RT_SCENE_TABLES_HLSLI
#define RT_SCENE_TABLES_HLSLI

// ============================================================
// レイトレーシングのシーンの材質の表（RTScene が毎フレーム送る）
// 表の行は TLAS のインスタンスと同じ並びで InstanceID() で引き、
// サブメッシュの行は firstSubMesh + GeometryIndex() で引く。
// 表・頂点・テクスチャは ResourceDescriptorHeap からヒープ内インデックスで引く
// （ルートシグネチャに CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED が要る）
// ============================================================

// テクスチャを繰り返して引くサンプラ
SamplerState gLinearWrap : register(s1);

static const uint kRTHitNoTexture = 0xFFFFFFFFu;
static const uint kRTHitSubMeshFlagLit = 1u << 0;
static const uint kRTHitSubMeshFlagDither = 1u << 1;
static const uint kRTHitSubMeshFlagMoving = 1u << 2;
static const uint kRTHitSubMeshFlagAlphaTest = 1u << 3;

// 頂点の並び（C++ 側 VertexData: 位置 16 + UV 8 + 法線 12 + 接線 12 + 頂点アニメーションの値 16。
// RayTracingSubsystem.cpp の static_assert と一致させる）
static const uint kRTHitVertexStride = 64u;
static const uint kRTHitVertexTexcoordOffset = 16u;
static const uint kRTHitVertexNormalOffset = 24u;
static const uint kRTHitVertexTangentOffset = 36u;

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
    uint normalTextureIndex;             // 法線テクスチャ（kRTHitNoTexture = 使わない）
    uint occlusionTextureIndex;          // AO テクスチャ（R。kRTHitNoTexture = 無し）
    float occlusionStrength;             // AO の強さ
    float pad;
};

/// @brief 材質の表 2 本
struct RTSceneTables
{
    uint instanceTableIndex;  // StructuredBuffer<RTHitInstance> のヒープ内インデックス
    uint subMeshTableIndex;   // StructuredBuffer<RTHitSubMesh> のヒープ内インデックス
    bool valid;               // 2 本ともそろっている
};

/// @brief ヒープ内インデックスから表を組む（どちらかが kRTHitNoTexture なら表なし）
RTSceneTables MakeRTSceneTables(uint instanceTableIndex, uint subMeshTableIndex)
{
    RTSceneTables tables;
    tables.instanceTableIndex = instanceTableIndex;
    tables.subMeshTableIndex = subMeshTableIndex;
    tables.valid = (instanceTableIndex != kRTHitNoTexture) && (subMeshTableIndex != kRTHitNoTexture);
    return tables;
}

/// @brief 表なし（アルファを調べず、どの物も不透明として当てる）
RTSceneTables NoRTSceneTables()
{
    return MakeRTSceneTables(kRTHitNoTexture, kRTHitNoTexture);
}

/// @brief レイが当たった三角形を指す値
struct RTHitPrimitive
{
    uint instanceIndex;   // InstanceID()（TLAS の並び＝表の並び）
    uint geometryIndex;   // GeometryIndex()（BLAS の形の並び＝サブメッシュの並び）
    uint primitiveIndex;  // PrimitiveIndex()（形の中の三角形の番号）
    float2 barycentrics;  // 交点の重心座標
};

/// @brief 当たった三角形の頂点と材質
struct RTHitTriangle
{
    RTHitInstance instance;
    RTHitSubMesh subMesh;
    uint3 vertexOffsets;   // 3 頂点の頂点バッファ内のバイト位置
    float3 positions[3];   // 3 頂点のワールド座標
    float3 weights;        // 3 頂点の重み（重心座標）
    float3 faceNormal;     // 面の法線（レイが来た側を向く）
    float faceArea2;       // ワールドの三角形の面積の 2 倍
    float2 edgeUv1;        // UV の辺（頂点 0 → 1）
    float2 edgeUv2;        // UV の辺（頂点 0 → 2）
    float2 uv;             // 材質の UV 変換を掛けた交点の UV
    float uvPerMeterLog2;  // 三角形の「UV の長さ / ワールドの長さ」の log2
};

/// @brief 頂点位置をワールドへ移す
float3 TransformHitPosition(RTHitInstance instance, float3 position)
{
    return float3(
        dot(instance.objectToWorld[0].xyz, position) + instance.objectToWorld[0].w,
        dot(instance.objectToWorld[1].xyz, position) + instance.objectToWorld[1].w,
        dot(instance.objectToWorld[2].xyz, position) + instance.objectToWorld[2].w);
}

/// @brief 向き（接線）をワールドへ移す（3×3 部分を掛ける。長さは正規化しない）
float3 TransformHitDirection(RTHitInstance instance, float3 direction)
{
    return float3(
        dot(instance.objectToWorld[0].xyz, direction),
        dot(instance.objectToWorld[1].xyz, direction),
        dot(instance.objectToWorld[2].xyz, direction));
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

/// @brief 当たった三角形のサブメッシュの行（表がそろっていること）
RTHitSubMesh LoadRTHitSubMesh(RTSceneTables tables, RTHitInstance instance, uint geometryIndex)
{
    StructuredBuffer<RTHitSubMesh> subMeshes = ResourceDescriptorHeap[tables.subMeshTableIndex];
    return subMeshes[instance.firstSubMesh + min(geometryIndex, instance.subMeshCount - 1u)];
}

/// @brief 当たった三角形の頂点と材質を引く（表がそろっていること）
/// @param rayDirection レイの向き（正規化済み。面の法線をレイが来た側へ向ける）
RTHitTriangle LoadRTHitTriangle(RTSceneTables tables, RTHitPrimitive hit, float3 rayDirection)
{
    StructuredBuffer<RTHitInstance> instances = ResourceDescriptorHeap[tables.instanceTableIndex];
    RTHitTriangle tri;
    tri.instance = instances[hit.instanceIndex];
    tri.subMesh = LoadRTHitSubMesh(tables, tri.instance, hit.geometryIndex);
    ByteAddressBuffer indexBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(tri.instance.indexBufferIndex)];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(tri.instance.vertexBufferIndex)];

    // 形ごとの三角形の番号を、索引バッファ全体の番号へ直す
    const uint3 triangleIndices = indexBuffer.Load3((tri.subMesh.firstTriangle + hit.primitiveIndex) * 12u);
    float2 uvs[3];
    [unroll]
    for (uint v = 0; v < 3; ++v)
    {
        tri.vertexOffsets[v] = triangleIndices[v] * kRTHitVertexStride;
        tri.positions[v] = TransformHitPosition(tri.instance, asfloat(vertexBuffer.Load3(tri.vertexOffsets[v])));
        // 材質の UV 変換を掛けた UV
        const float3 uv1 = float3(asfloat(vertexBuffer.Load2(tri.vertexOffsets[v] + kRTHitVertexTexcoordOffset)), 1.0f);
        uvs[v] = float2(dot(tri.subMesh.uvTransformU.xyz, uv1), dot(tri.subMesh.uvTransformV.xyz, uv1));
    }
    tri.weights = float3(1.0f - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics.x, hit.barycentrics.y);

    // 面の向きをレイが来た側へそろえる
    float3 faceNormal = cross(tri.positions[1] - tri.positions[0], tri.positions[2] - tri.positions[0]);
    tri.faceArea2 = length(faceNormal);
    faceNormal = (tri.faceArea2 > 1.0e-12f) ? faceNormal / tri.faceArea2 : -rayDirection;
    tri.faceNormal = (dot(faceNormal, rayDirection) > 0.0f) ? -faceNormal : faceNormal;

    tri.uv = uvs[0] * tri.weights.x + uvs[1] * tri.weights.y + uvs[2] * tri.weights.z;

    // テクセル密度（三角形の UV の面積とワールドの面積の比）
    tri.edgeUv1 = uvs[1] - uvs[0];
    tri.edgeUv2 = uvs[2] - uvs[0];
    const float uvArea2 = abs(tri.edgeUv1.x * tri.edgeUv2.y - tri.edgeUv2.x * tri.edgeUv1.y);
    tri.uvPerMeterLog2 = 0.5f * log2(max(uvArea2, 1.0e-20f) / max(tri.faceArea2, 1.0e-20f));
    return tri;
}

/// @brief 交点でレイが面に落とす幅（m）の log2
/// @param coneWidth 交点でのレイの広がりの幅（m）
float RTHitFootprintLog2(RTHitTriangle tri, float3 rayDirection, float coneWidth)
{
    return log2(max(coneWidth, 1.0e-6f) / max(abs(dot(tri.faceNormal, rayDirection)), 0.05f));
}

/// @brief 当たった点で材質のテクスチャを引く
float4 SampleRTHitTexture(uint textureIndex, RTHitTriangle tri, float footprintLog2)
{
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];
    return texture.SampleLevel(gLinearWrap, tri.uv, ComputeHitTextureLod(texture, tri.uvPerMeterLog2, footprintLog2));
}

/// @brief 当たった点のベースカラー（ファクター × テクスチャ）
float4 SampleRTHitBaseColor(RTHitTriangle tri, float footprintLog2)
{
    float4 baseColor = tri.subMesh.baseColor;
    if (tri.subMesh.baseColorTextureIndex != kRTHitNoTexture)
    {
        baseColor *= SampleRTHitTexture(tri.subMesh.baseColorTextureIndex, tri, footprintLog2);
    }
    return baseColor;
}

/// @brief この値以下のアルファの所は抜ける（ラスタライズの ShouldDiscardByAlpha と同じ）
float RTHitAlphaThreshold(RTHitSubMesh subMesh)
{
    return ((subMesh.flags & kRTHitSubMeshFlagDither) != 0) ? 0.5f : subMesh.alphaCutoff;
}

/// @brief 当たった所がアルファで抜けるか（表が無いときと、アルファを調べない材質は false）
/// @param coneWidth 交点でのレイの広がりの幅（m。テクスチャのミップの選択に使う）
bool IsRTHitCutout(RTSceneTables tables, RTHitPrimitive hit, float3 rayDirection, float coneWidth)
{
    if (!tables.valid)
    {
        return false;
    }
    StructuredBuffer<RTHitInstance> instances = ResourceDescriptorHeap[tables.instanceTableIndex];
    const RTHitSubMesh subMesh = LoadRTHitSubMesh(tables, instances[hit.instanceIndex], hit.geometryIndex);
    if ((subMesh.flags & kRTHitSubMeshFlagAlphaTest) == 0)
    {
        return false;
    }
    const RTHitTriangle tri = LoadRTHitTriangle(tables, hit, rayDirection);
    const float4 baseColor = SampleRTHitBaseColor(tri, RTHitFootprintLog2(tri, rayDirection, coneWidth));
    return baseColor.a <= RTHitAlphaThreshold(tri.subMesh);
}

#endif // RT_SCENE_TABLES_HLSLI
