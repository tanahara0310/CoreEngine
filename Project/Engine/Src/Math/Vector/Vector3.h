#pragma once
#include <cassert>
#include <cmath>

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

        // 成分ごとの乗算(Vector3 * Vector3) — アダマール積
        /// @note HLSL の float3 * float3 と同じ意味。内積は Dot() を使うこと。
        Vector3 operator*(const Vector3& v) const
        {
            return { x * v.x, y * v.y, z * v.z };
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

        // 成分ごとの乗算(Vector3 *= Vector3)
        Vector3& operator*=(const Vector3& v)
        {
            x *= v.x;
            y *= v.y;
            z *= v.z;
            return *this;
        }

        //========================================
        // 　比較演算子
        //========================================

        /// @note 浮動小数点の厳密比較。CVar の変更検出のように
        ///       「値が書き換わったか」を見る用途を想定している。
        ///       計算結果同士の比較には誤差があるので使わないこと。
        bool operator==(const Vector3& v) const
        {
            return x == v.x && y == v.y && z == v.z;
        }

        bool operator!=(const Vector3& v) const
        {
            return !(*this == v);
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

    //========================================
    // 　汎用関数
    //========================================

    // 内積
    inline float Dot(const Vector3& a, const Vector3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }


    // 長さの二乗
    /// @note 大小比較や閾値判定は Length より
    ///       こちらを使うと平方根を省ける（閾値も二乗して比較する）。
    inline float LengthSquared(const Vector3& v)
    {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    // 長さ（大きさ）
    inline float Length(const Vector3& v)
    {
        return std::sqrtf(LengthSquared(v));
    }

    // 2点間の距離
    inline float Distance(const Vector3& a, const Vector3& b)
    {
        return Length(b - a);
    }

    // 2点間の距離の二乗
    inline float DistanceSquared(const Vector3& a, const Vector3& b)
    {
        return LengthSquared(b - a);
    }

    // 正規化
    inline Vector3 Normalize(const Vector3& v)
    {
        float length = Length(v);
        if (length == 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
        return { v.x / length, v.y / length, v.z / length };
    }

    // 外積
    inline Vector3 Cross(const Vector3& a, const Vector3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    /// @brief v を n 方向へ射影する
    /// @param n 射影先の軸（正規化されていなくてよい）
    inline Vector3 Project(const Vector3& v, const Vector3& n)
    {
        const float nLengthSq = Dot(n, n);
        assert(nLengthSq != 0.0f);
        return n * (Dot(v, n) / nLengthSq);
    }

    /// @brief 単位ベクトル同士の球面線形補間
    /// @param start 開始方向（正規化済み）
    /// @param end   終了方向（正規化済み）
    /// @param t     補間係数
    /// @return 単位ベクトル
    /// @note 平行・反平行（回転面が決まらない縮退）でも単位長を保つ。
    inline Vector3 Slerp(const Vector3& start, const Vector3& end, float t)
    {
        float dot = Dot(start, end);
        dot = dot < -1.0f ? -1.0f : (dot > 1.0f ? 1.0f : dot);

        // start と直交する成分。これが回転面を決める。
        Vector3 relative = end - dot * start;
        const float relativeLength = Length(relative);

        if (relativeLength < 1e-6f) {
            // 平行 or 反平行 → 直交成分が消えて回転面が決まらない。
            // ここを Normalize(0) に任せると結果が単位長でなくなる（長さ 0 になる）。
            if (dot > 0.0f) {
                return start;   // 平行: 補間しても start のまま
            }
            // 反平行: 180 度回転。どの面を通るかは決まらないので
            // start と直交する任意の軸を選ぶ（結果は必ず単位ベクトルになる）
            const Vector3 helper = (std::abs(start.x) < 0.9f)
                ? Vector3{ 1.0f, 0.0f, 0.0f }
                : Vector3{ 0.0f, 1.0f, 0.0f };
            relative = Normalize(helper - Dot(helper, start) * start);
        }
        else {
            relative = relative * (1.0f / relativeLength);
        }

        const float theta = std::acos(dot) * t;
        return std::cos(theta) * start + std::sin(theta) * relative;
    }
}
