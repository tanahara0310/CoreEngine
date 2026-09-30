// VertexAnimationDeform.CS.hlsl
// 頂点アニメーション（植物の揺れ・海草の寄せ返し）をかけた頂点を、レイトレーシング用に書き出す。
// 描画の頂点シェーダー（Include/Object/Object3dVertex.hlsli の VertexMain）と同じ ApplyVertexAnimation を
// 同じマテリアル定数・同じ時間（gVertexAnim）で呼ぶので、影や水面の反射に映る形が画面の揺れと一致する。
// 出力は元と同じ VertexData の並び（位置だけ差し替え）で、BLAS の頂点と、水面の映り込みの
// ヒットシェーディング（Include/RayTracing/RTHitShading.hlsli）の両方がこれを読む。
//
// サブメッシュ（マテリアル）ごとに 1 回ディスパッチし、そのインデックス範囲が指す頂点を書く
// （BLAS はインデックスが指す頂点しか読まない）。同じ頂点を複数のスレッドが書くことがあるが、値は同じ。
// C++ 側: Graphics/RayTracing/VertexAnimationDeformer.cpp
#include "../Include/Object/VertexAnimation.hlsli"

// 元の頂点（C++ 側 VertexData の 64 バイト）。StructuredBuffer でベクトル型に入る
// 16 バイト境界の詰め物を避けるため、Skinning.CS.hlsl と同じくスカラーで並べる
struct SourceVertex
{
    float px, py, pz, pw;
    float u, v;
    float nx, ny, nz;
    float tx, ty, tz;
    float a0, a1, a2, a3; // VertexData::animData
};

/// @brief ディスパッチごとの値（ルート定数。C++ 側 VertexAnimationDeformer::DeformConstants と一致させる）
struct DeformConstants
{
    float4x4 world; // ワールド行列（個体ごとの位相をワールド位置から作る）
    float4x4 worldInverseTranspose; // 風向きをオブジェクト空間へ移す
    uint indexStart; // このサブメッシュのインデックスの開始位置（LOD0）
    uint indexCount; // インデックス数
    uint vertexCount; // 頂点数（範囲外の添字は捨てる）
    uint padding;
};

StructuredBuffer<SourceVertex> gSourceVertices : register(t0);
StructuredBuffer<uint> gIndices : register(t1);
RWStructuredBuffer<SourceVertex> gOutputVertices : register(u0); // 揺れた頂点（VertexData の並び。位置が先頭）
ConstantBuffer<DeformConstants> gDeform : register(b1); // b0 = gMaterial, b9 = gVertexAnim

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gDeform.indexCount)
    {
        return;
    }
    const uint vertexIndex = gIndices[gDeform.indexStart + dispatchThreadID.x];
    if (vertexIndex >= gDeform.vertexCount)
    {
        return;
    }

    SourceVertex vertex = gSourceVertices[vertexIndex];
    float3 position = float3(vertex.px, vertex.py, vertex.pz);
    float3 prevPosition; // 使わない（コンパイラが計算ごと消す）
    ApplyVertexAnimation(float4(vertex.a0, vertex.a1, vertex.a2, vertex.a3), float3(vertex.nx, vertex.ny, vertex.nz),
        gDeform.world, gDeform.worldInverseTranspose, position, prevPosition);
    // 位置だけ差し替える（法線・UV は元のまま。揺れは小さいので映り込みの見た目の差は小さい）
    vertex.px = position.x;
    vertex.py = position.y;
    vertex.pz = position.z;
    gOutputVertices[vertexIndex] = vertex;
}
