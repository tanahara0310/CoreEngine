// ============================================================
// 水の物理定数
// C++ の WaterPhysics.h と同じ値を使う。
// ============================================================
#ifndef WATER_PHYSICS_INCLUDED
#define WATER_PHYSICS_INCLUDED

/// @brief 水の正面の反射率（F0）。屈折率 1.333 から求めた値
static const float kWaterFresnelF0 = 0.02f;
/// @brief 屈折の比（1/n）を求めるときの屈折率の下限
static const float kWaterMinRefractiveIndex = 1.001f;
/// @brief 波が砕ける波高と静水深の比（H / h）
static const float kWaterBreakingIndex = 0.78f;

#endif // WATER_PHYSICS_INCLUDED
