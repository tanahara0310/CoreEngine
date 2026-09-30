// VertexAnimationDeform.CS.hlsl
// 頂点アニメーション（植物の揺れ・海草の寄せ返し）をかけた位置を、レイトレーシングの BLAS 用に書き出す。
// 描画の頂点シェーダー（Include/Object/Object3dVertex.hlsli の VertexMain）と同じ ApplyVertexAnimation を
// 同じマテリアル定数・同じ時間（gVertexAnim）で呼ぶので、影や水面の反射に映る形が画面の揺れと一致する。
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
RWStructuredBuffer<float3> gOutputPositions : register(u0); // BLAS の頂点（R32G32B32_FLOAT、12 バイト）
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

    const SourceVertex src = gSourceVertices[vertexIndex];
    float3 position = float3(src.px, src.py, src.pz);
    float3 prevPosition; // 使わない（コンパイラが計算ごと消す）
    ApplyVertexAnimation(float4(src.a0, src.a1, src.a2, src.a3), float3(src.nx, src.ny, src.nz),
        gDeform.world, gDeform.worldInverseTranspose, position, prevPosition);
    gOutputPositions[vertexIndex] = position;
}
