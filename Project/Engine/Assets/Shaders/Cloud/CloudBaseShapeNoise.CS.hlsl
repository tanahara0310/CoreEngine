/// @file CloudBaseShapeNoise.CS.hlsl
/// @brief 雲のベース形状ノイズ（128^3 RGBA8, タイル可能）を生成する
/// @details R = Perlin-Worley（雲の綿的な基本形状）、GBA = 高周波 Worley FBM（侵食用）。
///          初回のみ / ノイズ形状パラメータ変更時のみ生成される。

#include "Common/CloudNoiseCommon.hlsli"

RWTexture3D<float4> gOutput : register(u0);

static const uint kSize = 128;

[numthreads(4, 4, 4)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= kSize || dtid.y >= kSize || dtid.z >= kSize)
    {
        return;
    }

    float3 uvw = (float3(dtid) + 0.5f) / float(kSize);

    // ドメインワープ: Perlin / Worley は整数格子上に特徴点を持つため、そのまま使うと
    // 雲塊や隙間が等間隔の格子状に並んで見える（規則的な穴あき模様の原因）。
    // 低周波のタイル可能 Perlin でサンプル座標自体を歪め、格子の規則性を崩す。
    // PerlinNoise3D はどの整数周波数でも周期 1 でタイルするため、
    // 「uvw + 周期 1 のベクトル場」への置換はタイル可能性を保つ。
    float3 warp = float3(
        PerlinNoise3D(uvw + float3(19.1f, 33.7f, 47.2f), 3.0f),
        PerlinNoise3D(uvw + float3(5.8f, 71.3f, 13.6f), 3.0f),
        PerlinNoise3D(uvw + float3(83.4f, 9.2f, 61.9f), 3.0f));
    uvw += warp * 0.08f;

    // このテクスチャは 128^3。WorleyFBM3D(uvw, f) は内部で f, 2f, 4f の 3 オクターブを
    // 合成するため、周波数が解像度のナイキスト限界（128/2=64）に近い・超えると、
    // 生成時点でエイリアシングした規則パターンがテクセルへ焼き込まれ、
    // レンダリング結果に格子模様（ギザギザ）として現れる。
    // WorleyFBM3D_Safe で各オクターブを安全上限（解像度の 1/4 = 32）にクランプする。
    static const float kMaxSafeFreq = 32.0f;

    // R: Perlin-Worley
    float perlin = PerlinFBM3D(uvw, 4.0f, 5);
    float worleyLow = WorleyFBM3D_Safe(uvw, 4.0f, kMaxSafeFreq);
    // Perlin を Worley FBM の下限で再マップ（綿状の塊を作る）
    float perlinWorley = Remap(perlin, 0.0f, 1.0f, worleyLow, 1.0f);

    // GBA: 侵食用の高周波 Worley FBM（すべて安全上限でクランプ）
    float worley8 = WorleyFBM3D_Safe(uvw, 8.0f, kMaxSafeFreq);
    float worley16 = WorleyFBM3D_Safe(uvw, 16.0f, kMaxSafeFreq);
    float worley32 = WorleyFBM3D_Safe(uvw, 32.0f, kMaxSafeFreq);

    gOutput[dtid] = float4(saturate(perlinWorley), worley8, worley16, worley32);
}
