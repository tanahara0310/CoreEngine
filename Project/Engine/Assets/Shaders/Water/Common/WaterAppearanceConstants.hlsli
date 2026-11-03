// ============================================================
// 水面の見た目の定数
// ラスタの水面・RT の反射・岸の泡の時間変化で同じ値を使う。
// ============================================================
#ifndef WATER_APPEARANCE_CONSTANTS_INCLUDED
#define WATER_APPEARANCE_CONSTANTS_INCLUDED

/// @brief 空キューブマップのミップの段数
static const float kWaterSkyEnvMipCount = 5.0f;
/// @brief 解像できないさざ波の実効ラフネス（空キューブマップのミップ選択に使う）
static const float kWaterReflectionMicroRoughness = 0.20f;
/// @brief 波が砕けた所の岸の泡の被覆率
static const float kWaterShoreFoamStrength = 0.9f;

#endif // WATER_APPEARANCE_CONSTANTS_INCLUDED
