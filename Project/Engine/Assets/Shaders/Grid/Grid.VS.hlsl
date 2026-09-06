// 無限グリッド（床）の頂点シェーダー。
//
// 頂点バッファは使わず、SV_VertexID から画面全体を覆う三角形を 1 枚組み立てる。
// 各頂点でニア平面上とファー平面上の座標を求めて渡すと、その 2 点を結ぶ線分が
// そのピクセルのカメラレイそのものになる。ニア/ファー平面上の点は NDC の x, y に
// 対して線形なので、ラスタライザの線形補間がそのまま正確なレイになる。
// 逆行列はカメラ相対なので、ここで得られる点もカメラを原点とした座標になる
//（絶対ワールド座標のままだと原点から離れたときに float の桁が足りなくなる）。
#include "GridCommon.hlsli"

/// @brief NDC 上の点を逆 ViewProjection でワールドへ戻す
float3 Unproject(float2 ndc, float ndcZ)
{
    const float4 world = mul(float4(ndc, ndcZ, 1.0f), invViewProjection);
    return world.xyz / world.w;
}

GridVSOutput main(uint vertexId : SV_VertexID)
{
    // 頂点 3 つで画面全体を覆う。uv は (0,0) (2,0) (0,2) なので NDC は (-1,-1) (3,-1) (-1,3)
    const float2 uv = float2(float((vertexId << 1) & 2), float(vertexId & 2));
    const float2 ndc = uv * 2.0f - 1.0f;

    GridVSOutput output;
    // 深度はピクセルシェーダーが SV_Depth で出すので、ここはニア平面（z = 0）に置くだけ
    output.position = float4(ndc, 0.0f, 1.0f);
    output.nearPoint = Unproject(ndc, 0.0f);
    output.farPoint = Unproject(ndc, 1.0f);
    return output;
}
