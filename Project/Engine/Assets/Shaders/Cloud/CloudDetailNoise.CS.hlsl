/// @file CloudDetailNoise.CS.hlsl
/// @brief 雲のディテールノイズ（32^3 RGBA8, タイル可能）を生成する
/// @details 雲の縁を侵食して wispy / billowy なディテールを与える高周波 Worley FBM。

#include "Common/CloudNoiseCommon.hlsli"

RWTexture3D<float4> gOutput : register(u0);

static const uint kSize = 32;

[numthreads(4, 4, 4)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= kSize || dtid.y >= kSize || dtid.z >= kSize)
    {
        return;
    }

    float3 uvw = (float3(dtid) + 0.5f) / float(kSize);

    // このテクスチャは 32^3。WorleyFBM3D(uvw, f) は内部で f, 2f, 4f を合成するため、
    // 周波数が解像度のナイキスト限界（32/2=16）に近い・超えると、生成時点で
    // エイリアシングした格子模様（ギザギザ）がテクセルに焼き込まれる。
    // WorleyFBM3D_Safe で各オクターブを安全上限（解像度の 1/4 = 8）にクランプする。
    static const float kMaxSafeFreq = 8.0f;

    float worley2 = WorleyFBM3D_Safe(uvw, 2.0f, kMaxSafeFreq);
    float worley4 = WorleyFBM3D_Safe(uvw, 4.0f, kMaxSafeFreq);
    float worley8 = WorleyFBM3D_Safe(uvw, 8.0f, kMaxSafeFreq);

    gOutput[dtid] = float4(worley2, worley4, worley8, 1.0f);
}
