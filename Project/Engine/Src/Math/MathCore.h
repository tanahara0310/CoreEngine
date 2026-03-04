#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Quaternion/Quaternion.h"
#include "EulerTransform.h"
#include <cmath>
#include <numbers>

/// @brief 数学ライブラリの中核機能を提供する名前空間

namespace CoreEngine
{
    namespace MathCore {

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
