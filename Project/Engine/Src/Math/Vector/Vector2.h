#pragma once
#include <cmath>


namespace CoreEngine
{
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

    Vector2 Normalize() const {
        float length = std::sqrtf(x * x + y * y);
        if (length == 0.0f) {
            return { 0.0f, 0.0f };
        }
        return { x / length, y / length };
    }

    float Length() const {
        return std::sqrtf(x * x + y * y);
    }

    float Dot(const Vector2& v) const {
        return x * v.x + y * v.y;
    }
};

// スカラーとベクトルの乗算 (float * Vector2)
inline Vector2 operator*(float scalar, const Vector2& v) {
    return { scalar * v.x, scalar * v.y };
}
}
