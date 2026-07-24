// Hi-Z（階層深度）ピラミッド構築: 2x2 の max リダクション
// 本エンジンは標準 Z（near=0 / far=1、クリア値 1.0）のため、タイル内の「最遠」深度を
// 保持するには max を取る。ソース端（奇数サイズの余り行・列）は最後の出力テクセルが
// すべて吸収し、深度情報の欠落による過剰カリング（誤って遮蔽扱い）を防ぐ。
// 1 パス目: SceneDepth（フル解像度）→ mip0（1/2 解像度）
// 2 パス目以降: mip(i-1) → mip(i)

cbuffer HiZBuildParams : register(b0)
{
    uint2 gDestSize;   // 出力ミップの解像度
    uint2 gSourceSize; // 入力（深度 or 1 段上のミップ）の解像度
};

Texture2D<float> gSourceDepth : register(t0);
RWTexture2D<float> gDestHiZ : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 coord = dispatchThreadID.xy;
    if (coord.x >= gDestSize.x || coord.y >= gDestSize.y) {
        return;
    }

    // 基本は 2x2 の footprint。最終行・列はソースの余りをすべて吸収する
    const uint2 begin = coord * 2;
    uint2 end = min(begin + uint2(2, 2), gSourceSize);
    if (coord.x == gDestSize.x - 1) {
        end.x = gSourceSize.x;
    }
    if (coord.y == gDestSize.y - 1) {
        end.y = gSourceSize.y;
    }

    float maxDepth = 0.0f;
    for (uint y = begin.y; y < end.y; ++y) {
        for (uint x = begin.x; x < end.x; ++x) {
            maxDepth = max(maxDepth, gSourceDepth.Load(int3(x, y, 0)));
        }
    }

    gDestHiZ[coord] = maxDepth;
}
