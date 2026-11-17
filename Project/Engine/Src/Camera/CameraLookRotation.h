#pragma once

#include "Math/Vector/Vector3.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    /// @brief 視点から注視先を向くオイラー角を求める（Camera::LookAt と同じ変換）
    /// @param roll 視線軸まわりの傾き [ラジアン]
    /// @return (ピッチ, ヨー, ロール) [ラジアン]。視点と注視先が重なるときは (0, 0, roll)
    inline Vector3 CameraLookRotation(const Vector3& eye, const Vector3& target, float roll)
    {
        const Vector3 delta = target - eye;
        if (LengthSquared(delta) <= 1.0e-12f) {
            // 視点と注視先が重なると向きが決まらない。真正面を向かせて発散を防ぐ。
            return { 0.0f, 0.0f, roll };
        }

        const Vector3 forward = Normalize(delta);

        // MakeAffine の回転は Rx * Ry * Rz（行ベクトル規約）。roll = 0 のとき
        // 第 3 行（前方軸）は (cosX sinY, -sinX, cosX cosY) になるので、
        // yaw = atan2(f.x, f.z) / pitch = asin(-f.y) が Camera::LookAt と厳密に一致する。
        return {
            std::asin(std::clamp(-forward.y, -1.0f, 1.0f)),
            std::atan2(forward.x, forward.z),
            roll
        };
    }
}
