
#pragma once

#include "Math/Vector/Vector3.h"
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

    //========================================
    // 　行の取り出し / 差し替え
    //========================================
    /// @details 行ベクトル規約なので、第 0〜2 行がそれぞれ基底軸（スケール込み）、
    ///          第 3 行が平行移動になる。以前はこれを呼び出し側で
    ///          `{ m.m[3][0], m.m[3][1], m.m[3][2] }` と手書きしており、
    ///          同じ式が engine 全体に散らばっていた。

    /// @brief 平行移動成分（第 3 行）を取り出す
    Vector3 GetTranslation() const {
        return { m[3][0], m[3][1], m[3][2] };
    }

    /// @brief 平行移動成分（第 3 行）を差し替える。回転・スケールは触らない
    void SetTranslation(const Vector3& translation) {
        m[3][0] = translation.x;
        m[3][1] = translation.y;
        m[3][2] = translation.z;
    }

    /// @brief X 基底軸（第 0 行）。スケールを含むので方向が欲しいときは Normalize すること
    Vector3 GetAxisX() const {
        return { m[0][0], m[0][1], m[0][2] };
    }

    /// @brief Y 基底軸（第 1 行）。スケールを含む
    Vector3 GetAxisY() const {
        return { m[1][0], m[1][1], m[1][2] };
    }

    /// @brief Z 基底軸（第 2 行）。スケールを含む
    Vector3 GetAxisZ() const {
        return { m[2][0], m[2][1], m[2][2] };
    }

    /// @brief 基底軸を差し替える（軸番号 0=X, 1=Y, 2=Z）
    void SetAxis(int index, const Vector3& axis) {
        m[index][0] = axis.x;
        m[index][1] = axis.y;
        m[index][2] = axis.z;
    }
};

static_assert(sizeof(Matrix4x4) == sizeof(DirectX::XMFLOAT4X4),
    "Matrix4x4 は XMFLOAT4X4 とメモリ互換であること（reinterpret_cast の前提）");

namespace MathCore
{
    /// @brief 行列だけで閉じる演算
    /// @details 「引数と戻り値が Matrix4x4 と Vector3（＋float）で閉じるか」で
    ///          この置き場所を決めている。Quaternion が絡むもの
    ///          （MakeAffine のクォータニオン版・MakeRotateMatrix）は
    ///          型をまたぐので MathCore.h 側にある。
    ///          実装はすべて MathCore.cpp（DirectXMath へ委譲）。
    namespace Matrix {
        // 基本演算
        /// @note 加算・減算・乗算は Matrix4x4 の演算子（+ - *）を使うこと。
        ///       以前はここに Add / Subtract / Multiply があり、
        ///       同じ演算に 2 通りの綴りが存在していた。
        Matrix4x4 Inverse(const Matrix4x4& m);
        Matrix4x4 Transpose(const Matrix4x4& m);
        Matrix4x4 Identity();

        // 変換行列生成
        Matrix4x4 Translation(const Vector3& translate);
        Matrix4x4 Scale(const Vector3& scale);
        Matrix4x4 RotationX(float radian);
        Matrix4x4 RotationY(float radian);
        Matrix4x4 RotationZ(float radian);
        Matrix4x4 MakeAffine(const Vector3& scale, const Vector3& rotate, const Vector3& translate);
        Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up);

        // 任意軸回転行列
        Matrix4x4 MakeRotateAxisAngle(const Vector3& axis, float radian);

        // 方向から方向への回転行列
        Matrix4x4 DirectionToDirection(const Vector3& from, const Vector3& to);

        // 分解
        void DecomposeToSRT(const Matrix4x4& matrix, Vector3& scale, Vector3& rotate, Vector3& translate);
    }
}
}
