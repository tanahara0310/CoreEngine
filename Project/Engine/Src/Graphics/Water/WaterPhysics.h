#pragma once

// ============================================================
// 水の物理定数
// シェーダの WaterPhysics.hlsli と同じ値を使う。
// ============================================================

namespace CoreEngine
{
    namespace WaterPhysics
    {
        /// @brief 空気 1 に対する水の屈折率
        inline constexpr float kRefractiveIndex = 1.333f;

        /// @brief 水の正面の反射率（F0）。屈折率 1.333 から求めた値
        inline constexpr float kFresnelF0 = 0.02f;
    }
}
