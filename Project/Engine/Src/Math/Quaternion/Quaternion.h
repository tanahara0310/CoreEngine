#pragma once

#include "Math/Vector/Vector3.h"

/// <summary>
/// クォータニオン構造体
/// </summary>

namespace CoreEngine
{
struct Quaternion {
    float x, y, z, w;

    //========================================
    // 　二項演算子
    //========================================

    // クォータニオン加算(Quaternion + Quaternion)
    Quaternion operator+(const Quaternion& q) const
    {
        return { x + q.x, y + q.y, z + q.z, w + q.w };
    }

    // クォータニオン減算(Quaternion - Quaternion)
    Quaternion operator-(const Quaternion& q) const
    {
        return { x - q.x, y - q.y, z - q.z, w - q.w };
    }

    // クォータニオン乗算(Quaternion * Quaternion)
    Quaternion operator*(const Quaternion& q) const
    {
        return {
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        };
    }

    // スカラー乗算(Quaternion * float)
    Quaternion operator*(float scalar) const
    {
        return { x * scalar, y * scalar, z * scalar, w * scalar };
    }

    // クォータニオン除算(Quaternion / float)
    Quaternion operator/(float scalar) const
    {
        return { x / scalar, y / scalar, z / scalar, w / scalar };
    }

    //========================================
    // 　複合代入演算子
    //========================================

    // クォータニオン加算(Quaternion += Quaternion)
    Quaternion& operator+=(const Quaternion& q)
    {
        x += q.x;
        y += q.y;
        z += q.z;
        w += q.w;
        return *this;
    }

    // クォータニオン減算(Quaternion -= Quaternion)
    Quaternion& operator-=(const Quaternion& q)
    {
        x -= q.x;
        y -= q.y;
        z -= q.z;
        w -= q.w;
        return *this;
    }

    // クォータニオン乗算(Quaternion *= Quaternion)
    Quaternion& operator*=(const Quaternion& q)
    {
        *this = *this * q;
        return *this;
    }

    // スカラー乗算(Quaternion *= float)
    Quaternion& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
        return *this;
    }

    // クォータニオン除算(Quaternion /= float)
    Quaternion& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        w /= scalar;
        return *this;
    }

    //========================================
    // 　比較演算子
    //========================================

    // 等価比較(Quaternion == Quaternion)
    bool operator==(const Quaternion& q) const
    {
        return x == q.x && y == q.y && z == q.z && w == q.w;
    }

    // 非等価比較(Quaternion != Quaternion)
    bool operator!=(const Quaternion& q) const
    {
        return !(*this == q);
    }
};

// スカラーとクォータニオンの乗算 (float * Quaternion)
inline Quaternion operator*(float scalar, const Quaternion& q)
{
    return { scalar * q.x, scalar * q.y, scalar * q.z, scalar * q.w };
}

//========================================
// 　単項演算子
//========================================

// 単項マイナス(-Quaternion) - 共役クォータニオンではなく符号反転
inline Quaternion operator-(const Quaternion& q)
{
    return { -q.x, -q.y, -q.z, -q.w };
}

// 単項プラス(+Quaternion)
inline Quaternion operator+(const Quaternion& q)
{
    return q; // 単項プラスは値をそのまま返す
}

namespace MathCore
{
    /// @brief クォータニオンだけで閉じる演算
    /// @details 行列を返す MakeRotateMatrix は型をまたぐので MathCore.h 側にある。
    ///          実装はすべて MathCore.cpp（DirectXMath へ委譲）。
    namespace QuaternionMath {
        // 基本演算
        /// @note 積は Quaternion の operator* を使うこと（ハミルトン積 lhs * rhs）。
        Quaternion Identity();
        Quaternion Conjugate(const Quaternion& q);
        float Norm(const Quaternion& q);
        Quaternion Normalize(const Quaternion& q);
        Quaternion Inverse(const Quaternion& q);

        // 回転関連
        Quaternion MakeRotateAxisAngle(const Vector3& axis, float radian);
        Vector3 RotateVector(const Vector3& vector, const Quaternion& quaternion);

        // 補間
        Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);
    }
}
}
