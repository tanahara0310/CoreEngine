#pragma once

#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
/// @brief 形状ごとの慣性モーメント（ローカル軸に沿った対角成分）
/// @note 非対角が 0 になる形状だけを扱うので、3x3 ではなく Vector3 で持てる。
namespace Inertia
{
    /// @brief 球の慣性モーメント（3 軸とも同じ）
    Vector3 ForSphere(float mass, float radius);

    /// @brief 箱の慣性モーメント
    /// @param size 各軸の辺の長さ
    Vector3 ForBox(float mass, const Vector3& size);

    /// @brief Y 軸に立てたカプセルの慣性モーメント
    /// @param radius        半径
    /// @param cylinderLength 半球を除いた円筒部分の長さ
    /// @note 円筒と 2 つの半球の質量を体積比で分け、合算する。
    Vector3 ForCapsule(float mass, float radius, float cylinderLength);

    /// @brief 成分ごとの逆数を取る（0 以下の成分は 0 のままにする＝その軸は回らない）
    Vector3 Invert(const Vector3& inertia);
}
}
