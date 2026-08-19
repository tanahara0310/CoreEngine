#pragma once
#include <cmath>


namespace CoreEngine
{
/// @brief 2 成分ベクトル
struct Vector2 {
    float x, y;

    // スカラー乗算(Vector2 * float)
    Vector2 operator*(float scalar) const {
        return { x * scalar, y * scalar };
    }

    // スカラー除算(Vector2 / float)
    Vector2 operator/(float scalar) const {
        return { x / scalar, y / scalar };
    }

    // ベクトル加算(Vector2 + Vector2)
    Vector2 operator+(const Vector2& v) const {
        return { x + v.x, y + v.y };
    }

    // ベクトル減算(Vector2 - Vector2)
    Vector2 operator-(const Vector2& v) const {
        return { x - v.x, y - v.y };
    }

    // 複合代入演算子
    Vector2& operator+=(const Vector2& v) {
        x += v.x;
        y += v.y;
        return *this;
    }
    Vector2& operator-=(const Vector2& v) {
        x -= v.x;
        y -= v.y;
        return *this;
    }
    Vector2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }
    Vector2& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    // 成分ごとの乗算(Vector2 * Vector2) — アダマール積
    Vector2 operator*(const Vector2& v) const {
        return { x * v.x, y * v.y };
    }

    Vector2& operator*=(const Vector2& v) {
        x *= v.x;
        y *= v.y;
        return *this;
    }

    //========================================
    // 　比較演算子
    //========================================

    /// @note 浮動小数点の厳密比較。値が書き換わったかを見る用途のみ。
    bool operator==(const Vector2& v) const {
        return x == v.x && y == v.y;
    }

    bool operator!=(const Vector2& v) const {
        return !(*this == v);
    }
};

// スカラーとベクトルの乗算 (float * Vector2)
inline Vector2 operator*(float scalar, const Vector2& v) {
    return { scalar * v.x, scalar * v.y };
}

//========================================
// 　単項演算子
//========================================

inline Vector2 operator-(const Vector2& v) {
    return { -v.x, -v.y };
}

inline Vector2 operator+(const Vector2& v) {
    return v; // 単項プラスは値をそのまま返す
}

//========================================
// 　汎用関数
//========================================
/// @note Vector3 / Vector4 と綴りを揃えるためフリー関数にしてある。

// 内積
inline float Dot(const Vector2& a, const Vector2& b) {
    return a.x * b.x + a.y * b.y;
}

// 長さの二乗
inline float LengthSquared(const Vector2& v) {
    return v.x * v.x + v.y * v.y;
}

// 長さ（大きさ）
inline float Length(const Vector2& v) {
    return std::sqrtf(LengthSquared(v));
}

// 2点間の距離
inline float Distance(const Vector2& a, const Vector2& b) {
    return Length(b - a);
}

// 正規化
inline Vector2 Normalize(const Vector2& v) {
    float length = Length(v);
    if (length == 0.0f) {
        return { 0.0f, 0.0f };
    }
    return { v.x / length, v.y / length };
}

// 外積のZ成分（2Dでは擬似外積＝符号付き面積）
inline float Cross(const Vector2& a, const Vector2& b) {
    return a.x * b.y - a.y * b.x;
}
}
