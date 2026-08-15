
#pragma once

#include <DirectXMath.h>

namespace CoreEngine
{
/// @brief 行優先 float[4][4]・行ベクトル規約（平行移動は m[3][*]）の 4x4 行列
/// @details メモリレイアウトは DirectX::XMFLOAT4X4 と同一で、演算の実体は
///          DirectXMath（SIMD）に委譲する。ここに XMMATRIX を直接持たせると
///          16 バイトアライメント要求で構造体サイズと配置が変わるため、
///          保持は素の float 配列のまま・演算時だけ Load/Store する。
struct Matrix4x4 {
    float m[4][4];

    // 演算子オーバーロード
    Matrix4x4 operator*(const Matrix4x4& other) const {
        Matrix4x4 result;
        const DirectX::XMMATRIX a = DirectX::XMLoadFloat4x4(
            reinterpret_cast<const DirectX::XMFLOAT4X4*>(this));
        const DirectX::XMMATRIX b = DirectX::XMLoadFloat4x4(
            reinterpret_cast<const DirectX::XMFLOAT4X4*>(&other));
        DirectX::XMStoreFloat4x4(
            reinterpret_cast<DirectX::XMFLOAT4X4*>(&result),
            DirectX::XMMatrixMultiply(a, b));
        return result;
    }

    Matrix4x4 operator+(const Matrix4x4& other) const {
        Matrix4x4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = m[i][j] + other.m[i][j];
            }
        }
        return result;
    }

    Matrix4x4 operator-(const Matrix4x4& other) const {
        Matrix4x4 result;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                result.m[i][j] = m[i][j] - other.m[i][j];
            }
        }
        return result;
    }

    Matrix4x4& operator*=(const Matrix4x4& other) {
        *this = *this * other;
        return *this;
    }

    Matrix4x4& operator+=(const Matrix4x4& other) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                m[i][j] += other.m[i][j];
            }
        }
        return *this;
    }

    Matrix4x4& operator-=(const Matrix4x4& other) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                m[i][j] -= other.m[i][j];
            }
        }
        return *this;
    }
};

static_assert(sizeof(Matrix4x4) == sizeof(DirectX::XMFLOAT4X4),
    "Matrix4x4 は XMFLOAT4X4 とメモリ互換であること（reinterpret_cast の前提）");
}
