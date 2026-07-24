// Hi-Z オクルージョンカリング判定
// ワールド AABB の 8 頂点をクリップ空間へ投影し、スクリーン矩形を覆う Hi-Z ミップの
// max 深度（最遠）と AABB の最小深度を比較する。標準 Z（near=0 / far=1）前提。
// 結果は 2 フレーム遅延で描画スキップに適用されるため、判定は保守的
// （迷ったら可視）に倒してディスオクルージョン時のポップインを抑える。

struct BoundsData
{
    float3 boundsMin;
    float pad0;
    float3 boundsMax;
    float pad1;
};

cbuffer HiZCullParams : register(b0)
{
    float4x4 gViewProj;    // メインカメラの View * Projection（行ベクトル規約）
    float2 gHiZSize;       // Hi-Z mip0 の解像度
    uint gMipCount;        // Hi-Z のミップ数
    uint gQueryCount;      // 判定対象数
    float gDepthBias;      // 保守側に倒す深度バイアス
    float gMinRectTexels;  // 画面占有カリング閾値（mip0 テクセル単位。0 で無効）
    float2 gPad;
};

StructuredBuffer<BoundsData> gBounds : register(t0);
Texture2D<float> gHiZ : register(t1);
RWStructuredBuffer<uint> gVisibility : register(u0); // 1 = 可視, 0 = 遮蔽

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint index = dispatchThreadID.x;
    if (index >= gQueryCount) {
        return;
    }

    const float3 bMin = gBounds[index].boundsMin;
    const float3 bMax = gBounds[index].boundsMax;

    float2 ndcMin = float2(1e30f, 1e30f);
    float2 ndcMax = float2(-1e30f, -1e30f);
    float minZ = 1e30f;

    [unroll]
    for (uint i = 0; i < 8; ++i) {
        const float3 corner = float3(
            (i & 1) ? bMax.x : bMin.x,
            (i & 2) ? bMax.y : bMin.y,
            (i & 4) ? bMax.z : bMin.z);

        const float4 clip = mul(float4(corner, 1.0f), gViewProj);

        // near 平面より手前に頂点がある場合は投影が破綻するため保守的に可視とする
        if (clip.w <= 1e-4f) {
            gVisibility[index] = 1;
            return;
        }

        const float3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        minZ = min(minZ, ndc.z);
    }

    // 完全に画面外（フラスタム外）は既存のフラスタムカリングに委ね、ここでは可視扱い
    if (ndcMax.x < -1.0f || ndcMin.x > 1.0f || ndcMax.y < -1.0f || ndcMin.y > 1.0f || minZ > 1.0f) {
        gVisibility[index] = 1;
        return;
    }

    minZ = saturate(minZ);

    // NDC → UV（y 反転）→ mip0 テクセル矩形
    const float2 uvMin = saturate(float2(ndcMin.x * 0.5f + 0.5f, 0.5f - ndcMax.y * 0.5f));
    const float2 uvMax = saturate(float2(ndcMax.x * 0.5f + 0.5f, 0.5f - ndcMin.y * 0.5f));
    const float2 rectMin = uvMin * gHiZSize;
    const float2 rectMax = uvMax * gHiZSize;

    // 画面占有カリング: 投影矩形が縦横ともに極小（既定 1 テクセル = スクリーン約 2px 未満）の
    // オブジェクトは、見えていても画面への寄与が無いため描画をスキップする。
    // 細長い物（幅 1px × 高さ 100px 等）は片側が閾値を超えるので描画される。
    if ((rectMax.x - rectMin.x) < gMinRectTexels && (rectMax.y - rectMin.y) < gMinRectTexels) {
        gVisibility[index] = 0;
        return;
    }

    // 矩形が kFootprintTexels テクセル以下に収まるミップを選び、正確な範囲をループする。
    // 粗いミップの 2x2 サンプルでは footprint が実際の矩形の外側（空や遠景の深度）まで
    // 拾ってしまい、遮蔽されていても常に「可視」になる（カリング率 0% の原因）。
    // Hi-Z 側は端テクセルが余り領域を吸収しているため、クランプは保守側に働く。
    const float kFootprintTexels = 4.0f;
    const float maxDim = max(max(rectMax.x - rectMin.x, rectMax.y - rectMin.y), 1.0f);
    const uint mip = min((uint) max(ceil(log2(maxDim / kFootprintTexels)), 0.0f), gMipCount - 1);

    const float scale = exp2(-(float) mip);
    const int2 mipSize = max(int2(gHiZSize * scale), int2(1, 1));
    const int2 texelMin = clamp(int2(floor(rectMin * scale)), int2(0, 0), mipSize - 1);
    // ミップ選択により通常は 1 次元 5 テクセル以下。安全のため最大 8 に制限する
    const int2 texelMax = clamp(int2(floor(rectMax * scale)), texelMin, min(texelMin + 7, mipSize - 1));

    float hiZMax = 0.0f;
    for (int ty = texelMin.y; ty <= texelMax.y; ++ty) {
        for (int tx = texelMin.x; tx <= texelMax.x; ++tx) {
            hiZMax = max(hiZMax, gHiZ.Load(int3(tx, ty, mip)));
        }
    }

    // AABB の最小深度が Hi-Z の最遠深度より手前なら可視
    gVisibility[index] = (minZ <= hiZMax + gDepthBias) ? 1 : 0;
}
