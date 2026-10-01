/// @file SkyEnvironmentDownsample.CS.hlsl
/// @brief 空キューブマップ（空＋雲）の 1 段粗いミップを、細かいミップの 2×2 の平均で作る

RWTexture2DArray<float4> gSourceMip : register(u0); // 入力: 1 段細かいミップの 6 面
RWTexture2DArray<float4> gDestMip : register(u1);   // 出力: 作るミップの 6 面

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width, height, faces;
    gDestMip.GetDimensions(width, height, faces);
    if (dtid.x >= width || dtid.y >= height || dtid.z >= faces)
    {
        return;
    }

    const uint2 source = dtid.xy * 2u;
    gDestMip[dtid] = 0.25f * (
        gSourceMip[uint3(source, dtid.z)] +
        gSourceMip[uint3(source + uint2(1u, 0u), dtid.z)] +
        gSourceMip[uint3(source + uint2(0u, 1u), dtid.z)] +
        gSourceMip[uint3(source + uint2(1u, 1u), dtid.z)]);
}
