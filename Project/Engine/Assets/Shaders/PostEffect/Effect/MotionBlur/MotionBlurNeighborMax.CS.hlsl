// MotionBlurNeighborMax.CS.hlsl - 隣接 3x3 タイルの最大速度を取る
//
// 【なぜ要るか】速い物体はタイル境界をまたいで隣のタイルの画素を汚す。
// 自タイルの最大速度しか知らないと、境界の画素だけブラーが切れて
// タイル格子が見える（ポッピング）。3x3 の max でこれを防ぐ。

#include "MotionBlurCommon.hlsli"

Texture2D<float2> gTileMax : register(t0);
RWTexture2D<float2> gOutput : register(u0);

cbuffer NeighborMaxParams : register(b0)
{
    uint2 tileCount;
    float2 pad;
};

static const uint kGroupSize = 8;

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= tileCount.x || dispatchId.y >= tileCount.y)
    {
        return;
    }

    float2 best = float2(0.0f, 0.0f);
    float bestSq = 0.0f;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 tile = clamp(int2(dispatchId.xy) + int2(dx, dy),
                                    int2(0, 0), int2(tileCount) - 1);
            const float2 v = gTileMax.Load(int3(tile, 0));
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
