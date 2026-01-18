#pragma once

/// <summary>
/// ベクトル構造体
/// </summary>

namespace CoreEngine
{
struct Vector3 {
    float x, y, z;

    //========================================
    // 　二項演算子
    //========================================

    // ベクトル加算(Vector3 + Vector3)
    Vector3 operator+(const Vector3& v) const
    {
        return { x + v.x, y + v.y, z + v.z };
    }

    // ベクトル減算(Vector3 - Vector3)
    Vector3 operator-(const Vector3& v) const
    {
        return { x - v.x, y - v.y, z - v.z };
    }

    // スカラー乗算(Vector3 * float)
    Vector3 operator*(float scalar) const
    {
        return { x * scalar, y * scalar, z * scalar };
    }

    // ベクトル除算(Vector3 / float)
    Vector3 operator/(float scalar) const
    {
        return { x / scalar, y / scalar, z / scalar };
    }

    //========================================
    // 　複合代入演算子
    //========================================

    // ベクトル加算(Vector3 += Vector3)
    Vector3& operator+=(const Vector3& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }

    // ベクトル減算(Vector3 -= Vector3)
    Vector3& operator-=(const Vector3& v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }

    // スカラー乗算(Vector3 *= float)
    Vector3& operator*=(float scalar)
    {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    // ベクトル除算
    Vector3& operator/=(float scalar)
    {
        x /= scalar;
        y /= scalar;
        z /= scalar;
        return *this;
    }
};

// スカラーとベクトルの乗算 (float * Vector3)
inline Vector3 operator*(float scalar, const Vector3& v)
{
    return { scalar * v.x, scalar * v.y, scalar * v.z };
}

//========================================
// 　単項演算子
//========================================

inline Vector3 operator-(const Vector3& v)
{
    return { -v.x, -v.y, -v.z };
}

inline Vector3 operator+(const Vector3& v)
{
    return v; // 単項プラスは値をそのまま返す
}
}
