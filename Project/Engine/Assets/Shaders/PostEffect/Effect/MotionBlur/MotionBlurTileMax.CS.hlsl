// MotionBlurTileMax.CS.hlsl - タイル（tileSize px 四方）ごとの最大速度を求める
//
// 【なぜタイル化するか】ギャザー本体は「自分の上を通過したかもしれない画素」を
// 探すために周囲をサンプリングするが、探索半径を画素ごとに変えると発散する。
// 先にタイル単位の最大速度を作っておけば、探索方向と距離をタイルで固定でき、
// 1画素あたり十数サンプルで済む（McGuire 方式）。

#include "MotionBlurCommon.hlsli"

Texture2D<float2> gVelocity : register(t0); // G-Buffer MotionVector（NDC差分）
RWTexture2D<float2> gOutput : register(u0); // タイル最大速度[px]

cbuffer TileMaxParams : register(b0)
{
    uint2 screenSize;      // フル解像度
    uint2 tileCount;       // タイルバッファの実寸法
    float shutterFraction; // シャッター開角度/360
    float maxBlurPixels;   // ブラー最大距離[px]
    uint tileSize;         // タイル一辺[px]
    float pad;
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= tileCount.x || dispatchId.y >= tileCount.y)
    {
        return;
    }

    const uint2 tileOrigin = dispatchId.xy * tileSize;
    const float2 screenSizeF = float2(screenSize);

    // タイル内の全画素からブラー距離が最大の速度ベクトルを選ぶ。
    // 「成分ごとの max」ではなく「長さが最大のベクトル」を取ること。
    // 成分 max は斜め移動で実在しない方向のベクトルを合成してしまう
    float2 best = float2(0.0f, 0.0f);
    float bestSq = 0.0f;

    for (uint y = 0; y < tileSize; ++y)
    {
        for (uint x = 0; x < tileSize; ++x)
        {
            const uint2 p = min(tileOrigin + uint2(x, y), screenSize - 1);
            const float2 ndc = gVelocity.Load(int3(p, 0));
            const float2 v = ScaleAndClampVelocity(
                NdcVelocityToPixels(ndc, screenSizeF), shutterFraction, maxBlurPixels);
            const float sq = dot(v, v);
            if (sq > bestSq)
            {
                bestSq = sq;
                best = v;
            }
        }
    }

    gOutput[dispatchId.xy] = best;
}
