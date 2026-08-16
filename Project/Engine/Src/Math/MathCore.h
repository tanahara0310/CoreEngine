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

        /// @brief 値を [0, 1] に収める
        inline float Saturate(float value) {
            return Clamp(value, 0.0f, 1.0f);
        }

        /// @brief 線形補間
        inline float Lerp(float start, float end, float t) {
            return start + t * (end - start);
        }

        /// @brief 線形補間（成分ごと）
        inline Vector2 Lerp(const Vector2& start, const Vector2& end, float t) {
            return {
                Lerp(start.x, end.x, t),
                Lerp(start.y, end.y, t)
            };
        }

        /// @brief 線形補間（成分ごと）
        inline Vector3 Lerp(const Vector3& start, const Vector3& end, float t) {
            return {
                Lerp(start.x, end.x, t),
                Lerp(start.y, end.y, t),
                Lerp(start.z, end.z, t)
            };
        }

        /// @brief 線形補間（成分ごと）。色の補間もこれを使う
        inline Vector4 Lerp(const Vector4& start, const Vector4& end, float t) {
            return {
                Lerp(start.x, end.x, t),
                Lerp(start.y, end.y, t),
                Lerp(start.z, end.z, t),
                Lerp(start.w, end.w, t)
            };
        }

        /// @brief エルミート補間。0..1 を滑らかな 0..1 へ（3t² - 2t³）
        /// @details 入力は [0,1] にクランプされる。
        ///          以前は同じ式が GridRenderer / AtmosphereManager /
        ///          FFTOceanSpectrumBuilder / ProjectView に個別定義されていた。
        inline float SmoothStep(float t) {
            t = Saturate(t);
            return t * t * (3.0f - 2.0f * t);
        }

        /// @brief HLSL の smoothstep と同じ。edge0 → edge1 の区間を滑らかに 0 → 1 へ
        /// @note edge0 == edge1 でもゼロ除算しないよう分母に下限を敷いている
        inline float SmoothStep(float edge0, float edge1, float x) {
            return SmoothStep((x - edge0) / (std::max)(edge1 - edge0, 1e-6f));
        }

        /// @brief 角度を (-π, π] の範囲へ折り返す
        /// @details 差分角の最短経路を取るときに使う。
        ///          std::remainder は 1 回の演算で丸めるので、
        ///          ループで 2π を足し引きする実装と違って
        ///          巨大な角度でも誤差が積まれず、inf でも停止しない。
        inline float NormalizeAngle(float radians) {
            return std::remainder(radians, Constants::kTwoPi);
        }

        //================================================
        // 型をまたぐ変換（行列 ⇔ クォータニオン）
        //================================================
        /// @details 行列だけで閉じる演算は Matrix4x4.h、
        ///          クォータニオンだけで閉じる演算は Quaternion.h にある
        ///          （どちらもこのヘッダが include 済み）。
        ///          ここに残すのは両方の型を必要とするものだけ。
        namespace Matrix {
            /// @brief スケール・回転（クォータニオン）・平行移動からアフィン行列を作る
            Matrix4x4 MakeAffine(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);
        }

        namespace QuaternionMath {
            /// @brief クォータニオンを回転行列へ変換する
            Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion);
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
