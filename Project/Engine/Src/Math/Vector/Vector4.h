#pragma once
#include <cmath>

/// <summary>
/// 4成分ベクトル構造体
/// </summary>
/// @note 演算面は Vector3 / Quaternion と同じ綴りで揃えてある。
///       以前はこの型が「x,y,z,w を持つだけ」で演算子が 1 つも無く、
///       色の Lerp や等価判定が呼び出し側で成分ごとに手書きされていた。

namespace CoreEngine
{
    struct Vector4 {
        float x, y, z, w;

        //========================================
        // 　二項演算子
        //========================================

        // ベクトル加算(Vector4 + Vector4)
        Vector4 operator+(const Vector4& v) const
        {
            return { x + v.x, y + v.y, z + v.z, w + v.w };
        }

        // ベクトル減算(Vector4 - Vector4)
        Vector4 operator-(const Vector4& v) const
        {
            return { x - v.x, y - v.y, z - v.z, w - v.w };
        }

        // スカラー乗算(Vector4 * float)
        Vector4 operator*(float scalar) const
        {
            return { x * scalar, y * scalar, z * scalar, w * scalar };
        }

        // 成分ごとの乗算(Vector4 * Vector4) — アダマール積
        /// @note HLSL の float4 * float4 と同じ意味。内積は Dot() を使うこと。
        Vector4 operator*(const Vector4& v) const
        {
            return { x * v.x, y * v.y, z * v.z, w * v.w };
        }

        // スカラー除算(Vector4 / float)
        Vector4 operator/(float scalar) const
        {
            return { x / scalar, y / scalar, z / scalar, w / scalar };
        }

        //========================================
        // 　複合代入演算子
        //========================================

        Vector4& operator+=(const Vector4& v)
        {
            x += v.x;
            y += v.y;
            z += v.z;
            w += v.w;
            return *this;
        }

        Vector4& operator-=(const Vector4& v)
        {
            x -= v.x;
            y -= v.y;
            z -= v.z;
            w -= v.w;
            return *this;
        }

        Vector4& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            w *= scalar;
            return *this;
        }

        Vector4& operator*=(const Vector4& v)
        {
            x *= v.x;
            y *= v.y;
            z *= v.z;
            w *= v.w;
            return *this;
        }

        Vector4& operator/=(float scalar)
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

        /// @note 浮動小数点の厳密比較。CVar の変更検出のように
        ///       「値が書き換わったか」を見る用途を想定している。
        ///       計算結果同士の比較には誤差があるので使わないこと。
        bool operator==(const Vector4& v) const
        {
            return x == v.x && y == v.y && z == v.z && w == v.w;
        }

        bool operator!=(const Vector4& v) const
        {
            return !(*this == v);
        }
    };

    // スカラーとベクトルの乗算 (float * Vector4)
    inline Vector4 operator*(float scalar, const Vector4& v)
    {
        return { scalar * v.x, scalar * v.y, scalar * v.z, scalar * v.w };
    }

    //========================================
    // 　単項演算子
    //========================================

    inline Vector4 operator-(const Vector4& v)
    {
        return { -v.x, -v.y, -v.z, -v.w };
    }

    inline Vector4 operator+(const Vector4& v)
    {
        return v; // 単項プラスは値をそのまま返す
    }

    //========================================
    // 　汎用関数
    //========================================

    // 内積
    inline float Dot(const Vector4& a, const Vector4& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    // 長さの二乗
    inline float LengthSquared(const Vector4& v)
    {
        return v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w;
    }

    // 長さ（大きさ）
    inline float Length(const Vector4& v)
    {
        return std::sqrtf(LengthSquared(v));
    }

    // 正規化
    inline Vector4 Normalize(const Vector4& v)
    {
        float length = Length(v);
        if (length == 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f, 0.0f };
        }
        return { v.x / length, v.y / length, v.z / length, v.w / length };
    }
}
