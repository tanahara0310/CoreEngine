

// 頂点データ（position/texcoord/normal/tangent）
// C++側 VertexData (Vector4 + Vector2 + Vector3 + Vector3 = 48byte) とタイトパッキングを一致させるため、
// StructuredBufferでのベクトル型16byte境界パディングを避け、全てスカラーで宣言する。
struct SkinnedVertexGPU
{
    float posX, posY, posZ, posW;
    float u, v;
    float nX, nY, nZ;
    float tX, tY, tZ;
};

// 頂点ごとのボーン影響情報（C++側 VertexInfluence と一致）
struct InfluenceGPU
{
    float w0, w1, w2, w3;
    int i0, i1, i2, i3;
};

// GPU側のWellの定義（既存スキニングVSと同一レイアウト）
struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

StructuredBuffer<SkinnedVertexGPU> gSourceVertices : register(t0);
StructuredBuffer<InfluenceGPU> gInfluences : register(t1);
StructuredBuffer<Well> gMatrixPalette : register(t2);
RWStructuredBuffer<SkinnedVertexGPU> gOutputVertices : register(u0);

cbuffer SkinningParams : register(b0)
{
    uint gVertexCount;
    uint3 gPad;
};

static const uint kGroupSize = 64;

[numthreads(kGroupSize, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint vertexIndex = dispatchThreadID.x;
    if (vertexIndex >= gVertexCount)
    {
        return;
    }

    SkinnedVertexGPU src = gSourceVertices[vertexIndex];
    InfluenceGPU influence = gInfluences[vertexIndex];

    float4 position = float4(src.posX, src.posY, src.posZ, src.posW);
    float3 normal = float3(src.nX, src.nY, src.nZ);
    float3 tangent = float3(src.tX, src.tY, src.tZ);

    float weights[4] = { influence.w0, influence.w1, influence.w2, influence.w3 };
    int indices[4] = { influence.i0, influence.i1, influence.i2, influence.i3 };

    // 位置の変換
    float4 skinnedPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 skinnedNormal = float3(0.0f, 0.0f, 0.0f);
    float3 skinnedTangent = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        Well well = gMatrixPalette[indices[i]];
        skinnedPosition += mul(position, well.skeletonSpaceMatrix) * weights[i];
        skinnedNormal += mul(normal, (float3x3) well.skeletonSpaceInverseTransposeMatrix) * weights[i];
        skinnedTangent += mul(tangent, (float3x3) well.skeletonSpaceMatrix) * weights[i];
    }

    skinnedPosition.w = 1.0f; // 確実にwを1にする
    skinnedNormal = normalize(skinnedNormal);
    skinnedTangent = normalize(skinnedTangent);

    SkinnedVertexGPU dst;
    dst.posX = skinnedPosition.x;
    dst.posY = skinnedPosition.y;
    dst.posZ = skinnedPosition.z;
    dst.posW = skinnedPosition.w;
    dst.u = src.u;
    dst.v = src.v;
    dst.nX = skinnedNormal.x;
    dst.nY = skinnedNormal.y;
    dst.nZ = skinnedNormal.z;
    dst.tX = skinnedTangent.x;
    dst.tY = skinnedTangent.y;
    dst.tZ = skinnedTangent.z;

    gOutputVertices[vertexIndex] = dst;
}
