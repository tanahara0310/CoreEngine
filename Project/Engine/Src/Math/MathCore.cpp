#include "pch.h"
#include "Math/MathCore.h"
#include <algorithm>
#include <cassert>
#include <DirectXMath.h>

/// @file
/// @details 行列・クォータニオン演算の実体は DirectXMath（SIMD）へ委譲する。
///          API（Matrix4x4 / Quaternion / MathCore 名前空間）は自作のまま維持し、
///          このファイル内だけで Load/Store 変換する方針。
///          規約の対応は scratchpad の等価性テストで数値検証済み（1000 回 x 15 演算）:
///          - 本エンジンは行ベクトル規約・左手系 → XM も同じ（LookAtLH / PerspectiveFovLH が一致）
///          - MakeAffine のオイラー角は Rx*Ry*Rz 合成。XMMatrixRotationRollPitchYaw は
///            Rz*Rx*Ry 順で一致しないため使用禁止
///          - XMQuaternionMultiply(Q1, Q2) は Q2*Q1 を返すため、lhs*rhs は引数を逆順で渡す

namespace CoreEngine
{
    namespace MathCore {

        //================================================
        // DirectXMath との相互変換ヘルパー
        //================================================
        namespace {
            // reinterpret_cast の前提となるレイアウト互換の保証
            static_assert(sizeof(Matrix4x4) == sizeof(DirectX::XMFLOAT4X4),
                "Matrix4x4 と XMFLOAT4X4 はメモリ互換であること");
            static_assert(sizeof(Vector3) == sizeof(DirectX::XMFLOAT3),
                "Vector3 と XMFLOAT3 はメモリ互換であること");
            static_assert(sizeof(Vector4) == sizeof(DirectX::XMFLOAT4),
                "Vector4 と XMFLOAT4 はメモリ互換であること");
            static_assert(sizeof(Quaternion) == sizeof(DirectX::XMFLOAT4),
                "Quaternion(x,y,z,w) と XMFLOAT4 はメモリ互換であること");

            inline DirectX::XMMATRIX LoadM(const Matrix4x4& m) {
                return DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(&m));
            }
            inline Matrix4x4 StoreM(DirectX::FXMMATRIX m) {
                Matrix4x4 result;
                DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&result), m);
                return result;
            }
            inline DirectX::XMVECTOR LoadV3(const Vector3& v) {
                return DirectX::XMLoadFloat3(reinterpret_cast<const DirectX::XMFLOAT3*>(&v));
            }
            inline Vector3 StoreV3(DirectX::FXMVECTOR v) {
                Vector3 result;
                DirectX::XMStoreFloat3(reinterpret_cast<DirectX::XMFLOAT3*>(&result), v);
                return result;
            }
            inline DirectX::XMVECTOR LoadV4(const Vector4& v) {
                return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&v));
            }
            inline Vector4 StoreV4(DirectX::FXMVECTOR v) {
                Vector4 result;
                DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&result), v);
                return result;
            }
            inline DirectX::XMVECTOR LoadQ(const Quaternion& q) {
                return DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&q));
            }
            inline Quaternion StoreQ(DirectX::FXMVECTOR v) {
                Quaternion result;
                DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&result), v);
                return result;
            }
        }

        //================================================
        // 行列演算の実装
        //================================================
        namespace Matrix {
            // 加算・減算・乗算は Matrix4x4 の演算子（+ - *）にある

            Matrix4x4 Inverse(const Matrix4x4& m) {
                DirectX::XMVECTOR det;
                DirectX::XMMATRIX invMat = DirectX::XMMatrixInverse(&det, LoadM(m));

                // 特異行列は従来仕様どおり単位行列を返す
                if (DirectX::XMVectorGetX(det) == 0.0f) {
                    return Identity();
                }
                return StoreM(invMat);
            }

            Matrix4x4 Transpose(const Matrix4x4& m) {
                return StoreM(DirectX::XMMatrixTranspose(LoadM(m)));
            }

            Matrix4x4 Identity() {
                return StoreM(DirectX::XMMatrixIdentity());
            }

            Matrix4x4 Translation(const Vector3& translate) {
                return StoreM(DirectX::XMMatrixTranslation(translate.x, translate.y, translate.z));
            }

            Matrix4x4 Scale(const Vector3& scale) {
                return StoreM(DirectX::XMMatrixScaling(scale.x, scale.y, scale.z));
            }

            Matrix4x4 RotationX(float radian) {
                return StoreM(DirectX::XMMatrixRotationX(radian));
            }

            Matrix4x4 RotationY(float radian) {
                return StoreM(DirectX::XMMatrixRotationY(radian));
            }

            Matrix4x4 RotationZ(float radian) {
                return StoreM(DirectX::XMMatrixRotationZ(radian));
            }

            Matrix4x4 MakeAffine(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
                // 回転合成は本エンジン規約の Rx*Ry*Rz。
                // XMMatrixRotationRollPitchYaw は Rz*Rx*Ry 順のため結果が一致しない。
                const DirectX::XMMATRIX mat =
                    DirectX::XMMatrixScaling(scale.x, scale.y, scale.z)
                    * DirectX::XMMatrixRotationX(rotate.x)
                    * DirectX::XMMatrixRotationY(rotate.y)
                    * DirectX::XMMatrixRotationZ(rotate.z)
                    * DirectX::XMMatrixTranslation(translate.x, translate.y, translate.z);
                return StoreM(mat);
            }

            Matrix4x4 MakeAffine(const Vector3& scale, const Quaternion& rotate, const Vector3& translate) {
                const DirectX::XMMATRIX mat = DirectX::XMMatrixAffineTransformation(
                    LoadV3(scale), DirectX::XMVectorZero(), LoadQ(rotate), LoadV3(translate));
                return StoreM(mat);
            }

            Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
                return StoreM(DirectX::XMMatrixLookAtLH(LoadV3(eye), LoadV3(target), LoadV3(up)));
            }

            Matrix4x4 MakeRotateAxisAngle(const Vector3& axis, float radian) {
                // 0軸は単位行列（XMMatrixRotationAxis はゼロ軸を assert で落とす）
                if (Length(axis) == 0.0f) {
                    return Identity();
                }
                return StoreM(DirectX::XMMatrixRotationAxis(LoadV3(axis), radian));
            }

            Matrix4x4 DirectionToDirection(const Vector3& from, const Vector3& to) {
                // 入力ベクトルを正規化
                Vector3 normalizedFrom = Normalize(from);
                Vector3 normalizedTo = Normalize(to);

                // 同じ方向の場合は単位行列
                float dot = Dot(normalizedFrom, normalizedTo);

                // 浮動小数点の誤差を考慮して判定
                const float epsilon = 1e-6f;

                // fromとtoが同じ方向の場合（内積が1に近い）
                if (dot > 1.0f - epsilon) {
                    return Identity();
                }

                // fromとtoが逆方向の場合（内積が-1に近い）
                if (dot < -1.0f + epsilon) {
                    // 垂直なベクトルを見つけて180度回転
                    Vector3 perpendicular;

                    // fromのx成分が0に近くない場合
                    if (std::abs(normalizedFrom.x) > epsilon) {
                        perpendicular = Normalize(Vector3{ -normalizedFrom.y, normalizedFrom.x, 0.0f });
                    }
                    // fromのy成分が0に近くない場合
                    else if (std::abs(normalizedFrom.y) > epsilon) {
                        perpendicular = Normalize(Vector3{ 0.0f, -normalizedFrom.z, normalizedFrom.y });
                    }
                    // fromのz成分が0に近くない場合
                    else {
                        perpendicular = Normalize(Vector3{ normalizedFrom.z, 0.0f, -normalizedFrom.x });
                    }

                    // 180度回転（π rad）
                    return MakeRotateAxisAngle(perpendicular, std::numbers::pi_v<float>);
                }

                // 回転軸を外積で求める
                Vector3 axis = Cross(normalizedFrom, normalizedTo);

                // 回転角度を内積から求める
                float angle = std::acosf(std::clamp(dot, -1.0f, 1.0f));

                // 任意軸回転行列を作成
                return MakeRotateAxisAngle(axis, angle);
            }

            void DecomposeToSRT(const Matrix4x4& matrix, Vector3& scale, Vector3& rotate, Vector3& translate) {
                // XMMatrixDecompose は回転をクォータニオンで返すため、
                // オイラー角(XYZ)へ落とすここの仕様は手実装のまま維持する
                translate = matrix.GetTranslation();

                scale.x = Length(matrix.GetAxisX());
                scale.y = Length(matrix.GetAxisY());
                scale.z = Length(matrix.GetAxisZ());

                rotate.y = std::atan2(matrix.m[0][2], matrix.m[2][2]);
                rotate.x = std::atan2(-matrix.m[1][2], std::sqrt(matrix.m[1][0] * matrix.m[1][0] + matrix.m[1][1] * matrix.m[1][1]));
                rotate.z = std::atan2(matrix.m[1][0], matrix.m[1][1]);
            }
        }


        //================================================
        // クォータニオン演算の実装
        //================================================
        namespace QuaternionMath {
            // 積は Quaternion::operator* にある（ハミルトン積 lhs * rhs）

            Quaternion Identity() {
                return StoreQ(DirectX::XMQuaternionIdentity());
            }

            Quaternion Conjugate(const Quaternion& q) {
                return StoreQ(DirectX::XMQuaternionConjugate(LoadQ(q)));
            }

            float Norm(const Quaternion& q) {
                return DirectX::XMVectorGetX(DirectX::XMQuaternionLength(LoadQ(q)));
            }

            Quaternion Normalize(const Quaternion& q) {
                assert(Norm(q) != 0.0f); // ゼロクォータニオンの正規化を防ぐ
                return StoreQ(DirectX::XMQuaternionNormalize(LoadQ(q)));
            }

            Quaternion Inverse(const Quaternion& q) {
                assert(Norm(q) != 0.0f);
                return StoreQ(DirectX::XMQuaternionInverse(LoadQ(q)));
            }

            Quaternion MakeRotateAxisAngle(const Vector3& axis, float radian) {
                // ゼロ軸の場合は単位クォータニオン（XMQuaternionRotationAxis はゼロ軸を assert で落とす）
                if (Length(axis) == 0.0f) {
                    return Identity();
                }
                return StoreQ(DirectX::XMQuaternionRotationAxis(LoadV3(axis), radian));
            }

            Vector3 RotateVector(const Vector3& vector, const Quaternion& quaternion) {
                return StoreV3(DirectX::XMVector3Rotate(LoadV3(vector), LoadQ(quaternion)));
            }

            Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion) {
                return StoreM(DirectX::XMMatrixRotationQuaternion(LoadQ(quaternion)));
            }

            Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t) {
                // XMQuaternionSlerp は内積が負のとき反転して最短経路を取る（従来実装と同仕様）
                return StoreQ(DirectX::XMQuaternionSlerp(LoadQ(q0), LoadQ(q1), t));
            }
        }

        //================================================
        // 座標変換の実装
        //================================================
        namespace CoordinateTransform {
            Vector3 TransformCoord(const Vector3& vector, const Matrix4x4& matrix) {
                // w 除算込みの変換（XMVector3TransformCoord も同じく w で除算する）
                return StoreV3(DirectX::XMVector3TransformCoord(LoadV3(vector), LoadM(matrix)));
            }

            Vector4 TransformCoord(const Vector4& vector, const Matrix4x4& matrix) {
                // Vector4 版は w 除算なしの素の変換
                return StoreV4(DirectX::XMVector4Transform(LoadV4(vector), LoadM(matrix)));
            }

            Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) {
                return StoreV3(DirectX::XMVector3TransformNormal(LoadV3(v), LoadM(m)));
            }
        }

        //================================================
        // レンダリングパイプラインの実装
        //================================================
        namespace Rendering {
            Matrix4x4 PerspectiveFov(float fovY, float aspectRatio, float nearClip, float farClip) {
                return StoreM(DirectX::XMMatrixPerspectiveFovLH(fovY, aspectRatio, nearClip, farClip));
            }

            Matrix4x4 Orthographic(float left, float top, float right, float bottom, float nearClip, float farClip) {
                // 本エンジンの引数順 (left, top, right, bottom) と
                // XM の (left, right, bottom, top) の並びの違いに注意
                return StoreM(DirectX::XMMatrixOrthographicOffCenterLH(
                    left, right, bottom, top, nearClip, farClip));
            }

            Matrix4x4 Viewport(float left, float top, float width, float height, float minDepth, float maxDepth) {
                // XM に対応関数がないため手実装のまま
                return {
                    width / 2.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, -height / 2.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, maxDepth - minDepth, 0.0f,
                    left + width / 2.0f, top + height / 2.0f, minDepth, 1.0f
                };
            }
        }

        //================================================
        // 座標系変換の実装
        //================================================
        namespace Coordinate {
            Vector2 WorldToNormalizedScreen(const Vector3& worldPos, const Matrix4x4& viewMatrix,
                const Matrix4x4& projectionMatrix, float screenWidth, float screenHeight) {
                Matrix4x4 matVPV = viewMatrix * projectionMatrix * Rendering::Viewport(0, 0, screenWidth, screenHeight, 0.0f, 1.0f);

                Vector3 screenPos = CoordinateTransform::TransformCoord(worldPos, matVPV);

                Vector2 normalizedScreenPos;
                normalizedScreenPos.x = (screenPos.x / screenWidth) * 2.0f - 1.0f;
                normalizedScreenPos.y = -((screenPos.y / screenHeight) * 2.0f - 1.0f);

                return normalizedScreenPos;
            }

            Vector3 NormalizedScreenToWorld(const Vector2& normalizedScreenPos, float depth,
                const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix,
                float screenWidth, float screenHeight) {
                float screenX = (normalizedScreenPos.x + 1.0f) * 0.5f * screenWidth;
                float screenY = (-normalizedScreenPos.y + 1.0f) * 0.5f * screenHeight;

                Vector3 screenCoord = { screenX, screenY, depth };

                Matrix4x4 matVPV = viewMatrix * projectionMatrix * Rendering::Viewport(0, 0, screenWidth, screenHeight, 0.0f, 1.0f);
                Matrix4x4 matInvVPV = Matrix::Inverse(matVPV);

                return CoordinateTransform::TransformCoord(screenCoord, matInvVPV);
            }

            Vector3 NormalizedScreenToWorldWithDepth(const Vector2& normalizedScreenPos, const Vector3& originalWorldPos,
                const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix,
                float screenWidth, float screenHeight) {
                float screenX = (normalizedScreenPos.x + 1.0f) * 0.5f * screenWidth;
                float screenY = (-normalizedScreenPos.y + 1.0f) * 0.5f * screenHeight;

                Matrix4x4 matVPV = viewMatrix * projectionMatrix * Rendering::Viewport(0, 0, screenWidth, screenHeight, 0.0f, 1.0f);
                Vector3 originalScreenPos = CoordinateTransform::TransformCoord(originalWorldPos, matVPV);

                Vector3 targetScreenPos = { screenX, screenY, originalScreenPos.z };

                Matrix4x4 matInvVPV = Matrix::Inverse(matVPV);

                return CoordinateTransform::TransformCoord(targetScreenPos, matInvVPV);
            }
        }

    } // namespace MathCore
}
