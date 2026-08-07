#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Quaternion/Quaternion.h"
#include "EulerTransform.h"
#include <algorithm>
#include <cmath>
#include <numbers>

/// @brief 数学ライブラリの中核機能を提供する名前空間

namespace CoreEngine
{
    namespace MathCore {

        //================================================
        // 数学定数
        //================================================
        /// @details 円周率を各ファイルで書き写すと桁数がばらつく（3.14159f / 3.14159265f /
        ///          3.14159265358979323846f が混在していた）。ここに一本化する。
        ///          シェーダー側の対応物は Assets/Shaders/Include/Common/ShaderMath.hlsli。
        namespace Constants {
            inline constexpr float kPi = std::numbers::pi_v<float>;
            inline constexpr float kTwoPi = 2.0f * kPi;
            inline constexpr float kHalfPi = 0.5f * kPi;
            inline constexpr float kInvPi = std::numbers::inv_pi_v<float>;

            /// @brief 度 → ラジアン
            inline constexpr float kDegToRad = kPi / 180.0f;
            /// @brief ラジアン → 度
            inline constexpr float kRadToDeg = 180.0f / kPi;

            /// @brief 度をラジアンへ変換する
            inline constexpr float ToRadians(float degrees) { return degrees * kDegToRad; }
            /// @brief ラジアンを度へ変換する
            inline constexpr float ToDegrees(float radians) { return radians * kRadToDeg; }
        }

        //================================================
        // スカラー / ベクトル共通のユーティリティ
        //================================================
        /// @details 以前は CollisionUtils（当たり判定モジュール）に置かれており、
        ///          幾何と無関係なコードが当たり判定ヘッダを include する原因になっていた。

        /// @brief 値を [min, max] に収める
        inline float Clamp(float value, float min, float max) {
            return (std::max)(min, (std::min)(max, value));
        }

        /// @brief 各成分を [min, max] に収める
        inline Vector3 Clamp(const Vector3& value, const Vector3& min, const Vector3& max) {
            return {
                Clamp(value.x, min.x, max.x),
                Clamp(value.y, min.y, max.y),
                Clamp(value.z, min.z, max.z)
            };
        }

        /// @brief 線形補間
        inline float Lerp(float start, float end, float t) {
            return start + t * (end - start);
        }

        /// @brief 線形補間（成分ごと）
        inline Vector3 Lerp(const Vector3& start, const Vector3& end, float t) {
            return {
                Lerp(start.x, end.x, t),
                Lerp(start.y, end.y, t),
                Lerp(start.z, end.z, t)
            };
        }

        //================================================
        // ベクトル演算
        //================================================
        namespace Vector {
            // Vector3 演算（Vector3.hのグローバル関数を呼ぶラッパー）
            inline Vector3 Add(const Vector3& v1, const Vector3& v2) {
                return v1 + v2;
            }
            inline Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
                return v1 - v2;
            }
            inline Vector3 Multiply(float scalar, const Vector3& v) {
                return scalar * v;
            }
            inline float Dot(const Vector3& v1, const Vector3& v2) {
                return CoreEngine::Dot(v1, v2);
            }
            inline float Length(const Vector3& v) {
                return CoreEngine::Length(v);
            }
            inline Vector3 Normalize(const Vector3& v) {
                return CoreEngine::Normalize(v);
            }
            inline Vector3 Cross(const Vector3& v1, const Vector3& v2) {
                return CoreEngine::Cross(v1, v2);
            }

            // ベクトル投影
            Vector3 Project(const Vector3& v, const Vector3& n);

            /// @brief 単位ベクトル同士の球面線形補間
            /// @param start 開始方向（正規化済み）
            /// @param end   終了方向（正規化済み）
            /// @param t     補間係数
            /// @return 単位ベクトル
            /// @note 平行・反平行（回転面が決まらない縮退）でも単位長を保つ。
            Vector3 Slerp(const Vector3& start, const Vector3& end, float t);
        }

        //================================================
        // 行列演算
        //================================================
        namespace Matrix {
            // 基本演算
            Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);
            Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);
            Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
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
            Matrix4x4 MakeAffine(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);
            Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up);

            // 任意軸回転行列
            Matrix4x4 MakeRotateAxisAngle(const Vector3& axis, float radian);

            // 方向から方向への回転行列
            Matrix4x4 DirectionToDirection(const Vector3& from, const Vector3& to);

            // 分解
            void DecomposeToSRT(const Matrix4x4& matrix, Vector3& scale, Vector3& rotate, Vector3& translate);
        }

        //================================================
        // クォータニオン演算
        //================================================
        namespace QuaternionMath {
            // 基本演算
            Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs);
            Quaternion Identity();
            Quaternion Conjugate(const Quaternion& q);
            float Norm(const Quaternion& q);
            Quaternion Normalize(const Quaternion& q);
            Quaternion Inverse(const Quaternion& q);

            // 回転関連
            Quaternion MakeRotateAxisAngle(const Vector3& axis, float radian);
            Vector3 RotateVector(const Vector3& vector, const Quaternion& quaternion);
            Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion);

            // 補間
            Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);
        }

        //================================================
        // 座標変換
        //================================================
        namespace CoordinateTransform {
            // 基本変換
            Vector3 TransformCoord(const Vector3& vector, const Matrix4x4& matrix);
            Vector4 TransformCoord(const Vector4& vector, const Matrix4x4& matrix);
            Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m);
        }

        //================================================
        // レンダリングパイプライン
        //================================================
        namespace Rendering {
            // 投影行列
            Matrix4x4 PerspectiveFov(float fovY, float aspectRatio, float nearClip, float farClip);
            Matrix4x4 Orthographic(float left, float top, float right, float bottom, float nearClip, float farClip);
            Matrix4x4 Viewport(float left, float top, float width, float height, float minDepth, float maxDepth);
        }

        //================================================
        // 座標系変換
        //================================================
        namespace Coordinate {
            // スクリーン座標変換
            Vector2 WorldToNormalizedScreen(const Vector3& worldPos, const Matrix4x4& viewMatrix,
                const Matrix4x4& projectionMatrix, float screenWidth, float screenHeight);
            Vector3 NormalizedScreenToWorld(const Vector2& normalizedScreenPos, float depth,
                const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix,
                float screenWidth, float screenHeight);
            Vector3 NormalizedScreenToWorldWithDepth(const Vector2& normalizedScreenPos, const Vector3& originalWorldPos,
                const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix,
                float screenWidth, float screenHeight);
        }

    } // namespace MathCore

    // ========================================
    // 便利な名前空間エイリアス
    // ========================================
    // 新しいコードではこれらのエイリアスを使用することを推奨します
  /*  namespace Math = MathCore;
    namespace Vec = MathCore::Vector;
    namespace Mat = MathCore::Matrix;
    namespace Quat = MathCore::QuaternionMath;*/
}
