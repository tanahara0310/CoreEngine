/// @file CloudNoiseMip3D.CS.hlsl
/// @brief 3D ノイズの 1 段下のミップを 2x2x2 のボックス平均で作る
/// @details gSource は生成元ミップだけを見る SRV、gOutput は生成先ミップの UAV。
///          どのミップを扱うかは記述子側で決まるので定数バッファを持たない。

Texture3D<float4> gSource : register(t0);
RWTexture3D<float4> gOutput : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    uint depth;
    gOutput.GetDimensions(width, height, depth);
    if (dtid.x >= width || dtid.y >= height || dtid.z >= depth)
    {
        return;
    }

    int3 src = int3(dtid) * 2;

    float4 sum = 0.0f;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        int3 offset = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        sum += gSource.Load(int4(src + offset, 0));
    }

    gOutput[dtid] = sum * 0.125f;
}
