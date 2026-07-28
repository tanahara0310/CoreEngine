// ============================================================
// RT 水面屈折の出力アルファ エンコード規約
// ------------------------------------------------------------
// 書き手（RTWaterRefraction.hlsl）と読み手（Water.PS.hlsl）で規約が
// 二重定義されており、「必ず一致させること」というコメントだけで守られていた。
// ここへ集約して物理的に 1 つにする。
//
// 【なぜ 2 つの成否を分けて運ぶのか】
// 光路長 length(hitWorldPos - ray.Origin) はレイトレースの結果そのもので、
// レイがジオメトリにヒットしさえすれば常に正しい。一方、屈折色はヒット点を
// スクリーン空間へ再投影して SceneColor から拾う都合上、
//   ・再投影先が画面外（InvalidClip）
//   ・再投影先に別の面が写っている（DepthMismatch）
// のときに取得できない。この 2 つは独立した失敗である。
//
// 旧実装は両者を 1 つの成功/失敗フラグにまとめていたため、色が取れないだけの
// ピクセルで有効な光路長まで捨てられ、Water.PS が別の推定量（スクリーン空間近似）
// へ切り替わっていた。岸際では色の失敗が帯状に起きるため、その境界で水柱厚さが
// 不連続になり、波打ち際に白線が二重に見えていた。
//
// 【アルファのレンジ】出力は R16G16B16A16_FLOAT なので 1.0 超も格納できる。
//   [0, 0.5)   : ヒットなし。光路長も色も無効（理由コード 1〜9/255）
//   [0.5, 1.0] : ヒットあり・色も有効。光路長をパック
//   [1.5, 2.0] : ヒットあり・色は無効（rgb はフォールバック色）。光路長は同じ刻み
// ============================================================
#ifndef WATER_REFRACTION_ENCODING_INCLUDED
#define WATER_REFRACTION_ENCODING_INCLUDED

static const float kRTSuccessRangeMin = 0.5f;
static const float kRTMaxOpticalPathMeters = 64.0f;
static const float kRTColorInvalidOffset = 1.0f;

/// @brief ヒット済みピクセルのアルファを作る（RTWaterRefraction 側）
/// @param opticalPathLength 屈折レイが水面からヒット点まで進んだ距離（＝真の水柱厚さ）
/// @param colorValid        屈折色をスクリーン空間から取得できたか
float EncodeHitAlpha(float opticalPathLength, bool colorValid)
{
    const float normalized = saturate(opticalPathLength / kRTMaxOpticalPathMeters);
    const float packed = kRTSuccessRangeMin + normalized * (1.0f - kRTSuccessRangeMin);
    return colorValid ? packed : (packed + kRTColorInvalidOffset);
}

/// @brief 屈折レイがジオメトリにヒットしたか（＝光路長が使えるか）
float IsRTPathValid(float alpha)
{
    return alpha >= kRTSuccessRangeMin ? 1.0f : 0.0f;
}

/// @brief 屈折色をスクリーン空間から取得できたか
float IsRTColorValid(float alpha)
{
    return (alpha >= kRTSuccessRangeMin && alpha <= 1.0f) ? 1.0f : 0.0f;
}

/// @brief 屈折レイが実際に水中を進んだ光路長（メートル）を復元する
/// @details EncodeHitAlpha() の逆変換。この値は「表示されている屈折後の内容」に
///          対応する真の水柱厚さであり、スクリーン空間の素の深度
///          （屈折で曲げる前の深度）とは異なる。
float DecodeRTOpticalPath(float alpha)
{
    const float packed = (alpha > 1.0f) ? (alpha - kRTColorInvalidOffset) : alpha;
    const float normalized = saturate((packed - kRTSuccessRangeMin) / (1.0f - kRTSuccessRangeMin));
    return normalized * kRTMaxOpticalPathMeters;
}

#endif // WATER_REFRACTION_ENCODING_INCLUDED
