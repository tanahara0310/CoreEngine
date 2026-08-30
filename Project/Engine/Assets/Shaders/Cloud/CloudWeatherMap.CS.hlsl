/// @file CloudWeatherMap.CS.hlsl
/// @brief 手続き生成の天候マップ（512^2 RGBA8, タイル可能）を生成する
/// @details R = カバレッジ（雲の有無）、G = 雲タイプ（0:層雲〜1:積乱雲）、
///          B = 雲頂高度の倍率、A = 未使用。
///          エディタの配置ペイントはここへ焼かず、マーチ時にワールド固定領域として合成する
///          （タイルする天候マップへ焼くと、描いた雲が世界中に繰り返し現れるため）。

#include "Common/CloudNoiseCommon.hlsli"

RWTexture2D<float4> gOutput : register(u0);

static const uint kSize = 512;

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= kSize || dtid.y >= kSize)
    {
        return;
    }

    float2 uv = (float2(dtid.xy) + 0.5f) / float(kSize);

    // R: カバレッジ（広域の雲の有無）
    // Perlin FBM の出力は 0.5 付近に集中するため、そのままだとカバレッジが一様になり
    // 空全体が均質な板状の雲で覆われる。Remap でコントラストを広げ、
    // 大きな晴れ間（0）と厚い雲域（1）がはっきり現れるようにする。
    // 開始周波数が低い（3 等）と特徴サイズが 10km 超になり、個々の積雲ではなく
    // 巨大な一枚板の雲デッキになる。数 km スケールの独立した雲塊には 6 程度が適する。
    // ドメインワープ: Perlin の格子規則性で雲域・晴れ間が等間隔に並ぶのを崩し、
    // 前線状・帯状の自然な雲配置に寄せる（PerlinNoise2D は周期 1 でタイルする）。
    float2 warp = float2(
        PerlinNoise2D(uv + float2(11.3f, 27.9f), 2.0f),
        PerlinNoise2D(uv + float2(43.1f, 7.7f), 2.0f)) * 0.12f;

    float coverageNoise = PerlinFBM2D(uv + warp, 6.0f, 5);
    float coverage = saturate(Remap(coverageNoise, 0.34f, 0.70f, 0.0f, 1.0f));

    // 広域変調: さらに低周波の FBM で地方単位の「やや晴れた領域／やや曇った領域」を作る。
    // これが無いとカバレッジの統計が画面全域で一様になり、どこを見ても同じ密度の
    // 雲が等間隔に浮かぶ「壁紙」のような空になる。
    // 注意: 下限を下げすぎると（かつて 0.15 だった）地方規模で雲が事実上ゼロになる
    // 帯ができ、その境界に隣接する雲塊だけが「孤立したポップコーン状の粒」として
    // 取り残されて見える不具合を起こした。変調幅は 0.6〜1.15 程度の緩いレンジに留め、
    // 地域差は付けつつ「雲が丸ごと消える帯」を作らないこと。
    float regional = PerlinFBM2D(uv + float2(0.37f, 0.81f) + warp * 0.5f, 3.0f, 3);
    coverage *= saturate(Remap(regional, 0.30f, 0.62f, 0.6f, 1.15f));

    // G: 雲タイプ（別オフセットで相関を切る）。同様にコントラストを広げて
    //    層雲〜積乱雲の高度勾配が場所ごとに変わるようにする。
    float typeNoise = PerlinFBM2D(uv + float2(3.7f, 1.3f), 2.0f, 3);
    float cloudType = saturate(Remap(typeNoise, 0.38f, 0.68f, 0.0f, 1.0f));

    // B: 雲頂高度の倍率。雲タイプより高い周波数にして、隣り合う雲塊ごとに背丈が変わるようにする。
    // 雲タイプと同じ周波数だと変化が緩やかすぎて画面内の雲がほぼ同じ高さで揃う。
    float topNoise = PerlinFBM2D(uv + float2(17.9f, 5.3f), 9.0f, 3);
    float cloudTop = saturate(Remap(topNoise, 0.35f, 0.65f, 0.0f, 1.0f));

    gOutput[dtid.xy] = float4(coverage, cloudType, cloudTop, 1.0f);
}
