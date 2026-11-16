#pragma once

/// @file
/// @brief 一様 Catmull-Rom スプラインの補間

namespace CoreEngine
{
    namespace MathCore {

        /// @brief 一様 Catmull-Rom で p1 から p2 までの区間を補間する
        /// @tparam T float か、float との掛け算と足し算ができる型（Vector3 など）
        /// @param t 区間の中の位置（0 で p1、1 で p2）
        template <typename T>
        T CatmullRom(const T& p0, const T& p1, const T& p2, const T& p3, float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;

            const float b0 = -0.5f * t3 + t2 - 0.5f * t;
            const float b1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
            const float b2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
            const float b3 = 0.5f * t3 - 0.5f * t2;

            return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
        }
    }
}
