#pragma once

#include "Math/EulerTransform.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
/// @brief CPU で更新するパーティクル 1 粒の状態
struct Particle {
    EulerTransform transform;
    Vector3 velocity;
    Vector4 color;
    Vector4 initialColor;  ///< メインモジュールが決めた初期色（色の変化の起点）
    Vector3 initialScale;  ///< メインモジュールが決めた初期サイズ（サイズの変化の起点）
    float lifeTime;
    float currentTime;
    Vector3 rotationSpeed = { 0.0f, 0.0f, 0.0f };
};
}
