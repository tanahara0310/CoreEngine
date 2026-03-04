#include "Math/MathCore.h"
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
    namespace MathCore {

    //================================================
    // ベクトル演算の実装（Vector3.hに移動済み）
    //================================================
    // Vector::Add, Subtract, Multiply は演算子オーバーロードで実装
    // Vector::Dot, Length, Normalize, Cross は Vector3.h のグローバル関数として実装

    // Project関数のみこちらに残す（特殊な用途のため）
    namespace Vector {
        Vector3 Project(const Vector3& v, const Vector3& n) {
            float dotProduct = CoreEngine::Dot(v, n);
            float nLengthSq = CoreEngine::Dot(n, n);
            assert(nLengthSq != 0.0f);
            float scalar = dotProduct / nLengthSq;
            return scalar * n;
        }
    }

        //================================================
        // 行列演算の実装
        //================================================
        namespace Matrix {
            Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) {
                Matrix4x4 result;
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        result.m[i][j] = m1.m[i][j] + m2.m[i][j];
                    }
                }
                return result;
            }

            Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) {
                Matrix4x4 result;
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        result.m[i][j] = m1.m[i][j] - m2.m[i][j];
                    }
                }
                return result;
            }

            Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
                Matrix4x4 result;
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        result.m[i][j] = 0;
                        for (int k = 0; k < 4; ++k) {
                            result.m[i][j] += m1.m[i][k] * m2.m[k][j];
                        }
                    }
                }
                return result;
            }

            Matrix4x4 Inverse(const Matrix4x4& m) {
                Matrix4x4 result;
                float det;

                // 各要素の余因子を直接計算
                float cof[4][4];

                cof[0][0] = m.m[1][1] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) - m.m[1][2] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) + m.m[1][3] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]);
                cof[0][1] = -(m.m[1][0] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) - m.m[1][2] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) + m.m[1][3] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]));
                cof[0][2] = m.m[1][0] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) - m.m[1][1] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) + m.m[1][3] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0]);
                cof[0][3] = -(m.m[1][0] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]) - m.m[1][1] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]) + m.m[1][2] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0]));

                cof[1][0] = -(m.m[0][1] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) - m.m[0][2] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) + m.m[0][3] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]));
                cof[1][1] = m.m[0][0] * (m.m[2][2] * m.m[3][3] - m.m[2][3] * m.m[3][2]) - m.m[0][2] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) + m.m[0][3] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]);
                cof[1][2] = -(m.m[0][0] * (m.m[2][1] * m.m[3][3] - m.m[2][3] * m.m[3][1]) - m.m[0][1] * (m.m[2][0] * m.m[3][3] - m.m[2][3] * m.m[3][0]) + m.m[0][3] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0]));
                cof[1][3] = m.m[0][0] * (m.m[2][1] * m.m[3][2] - m.m[2][2] * m.m[3][1]) - m.m[0][1] * (m.m[2][0] * m.m[3][2] - m.m[2][2] * m.m[3][0]) + m.m[0][2] * (m.m[2][0] * m.m[3][1] - m.m[2][1] * m.m[3][0]);

                cof[2][0] = m.m[0][1] * (m.m[1][2] * m.m[3][3] - m.m[1][3] * m.m[3][2]) - m.m[0][2] * (m.m[1][1] * m.m[3][3] - m.m[1][3] * m.m[3][1]) + m.m[0][3] * (m.m[1][1] * m.m[3][2] - m.m[1][2] * m.m[3][1]);
                cof[2][1] = -(m.m[0][0] * (m.m[1][2] * m.m[3][3] - m.m[1][3] * m.m[3][2]) - m.m[0][2] * (m.m[1][0] * m.m[3][3] - m.m[1][3] * m.m[3][0]) + m.m[0][3] * (m.m[1][0] * m.m[3][2] - m.m[1][2] * m.m[3][0]));
                cof[2][2] = m.m[0][0] * (m.m[1][1] * m.m[3][3] - m.m[1][3] * m.m[3][1]) - m.m[0][1] * (m.m[1][0] * m.m[3][3] - m.m[1][3] * m.m[3][0]) + m.m[0][3] * (m.m[1][0] * m.m[3][1] - m.m[1][1] * m.m[3][0]);
                cof[2][3] = -(m.m[0][0] * (m.m[1][1] * m.m[3][2] - m.m[1][2] * m.m[3][1]) - m.m[0][1] * (m.m[1][0] * m.m[3][2] - m.m[1][2] * m.m[3][0]) + m.m[0][2] * (m.m[1][0] * m.m[3][1] - m.m[1][1] * m.m[3][0]));

                cof[3][0] = -(m.m[0][1] * (m.m[1][2] * m.m[2][3] - m.m[1][3] * m.m[2][2]) - m.m[0][2] * (m.m[1][1] * m.m[2][3] - m.m[1][3] * m.m[2][1]) + m.m[0][3] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]));
                cof[3][1] = m.m[0][0] * (m.m[1][2] * m.m[2][3] - m.m[1][3] * m.m[2][2]) - m.m[0][2] * (m.m[1][0] * m.m[2][3] - m.m[1][3] * m.m[2][0]) + m.m[0][3] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]);
                cof[3][2] = -(m.m[0][0] * (m.m[1][1] * m.m[2][3] - m.m[1][3] * m.m[2][1]) - m.m[0][1] * (m.m[1][0] * m.m[2][3] - m.m[1][3] * m.m[2][0]) + m.m[0][3] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]));
                cof[3][3] = m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) - m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) + m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);

                // 行列式の計算
                det = m.m[0][0] * cof[0][0] + m.m[0][1] * cof[0][1] + m.m[0][2] * cof[0][2] + m.m[0][3] * cof[0][3];

                if (det == 0.0f) {
                    return Identity();
                }

                // 逆行列の計算
                float invDet = 1.0f / det;
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        result.m[j][i] = cof[i][j] * invDet;
                    }
                }

                return result;
            }

            Matrix4x4 Transpose(const Matrix4x4& m) {
                Matrix4x4 result;
                for (int i = 0; i < 4; ++i) {
                    for (int j = 0; j < 4; ++j) {
                        result.m[i][j] = m.m[j][i];
                    }
                }
                return result;
            }

            Matrix4x4 Identity() {
                return {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
            }

            Matrix4x4 Translation(const Vector3& translate) {
                return {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    translate.x, translate.y, translate.z, 1.0f
                };
            }

            Matrix4x4 Scale(const Vector3& scale) {
                return {
                    scale.x, 0.0f, 0.0f, 0.0f,
                    0.0f, scale.y, 0.0f, 0.0f,
                    0.0f, 0.0f, scale.z, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
            }

            Matrix4x4 RotationX(float radian) {
                float cosR = std::cosf(radian);
                float sinR = std::sinf(radian);
                return {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, cosR, sinR, 0.0f,
                    0.0f, -sinR, cosR, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
            }

            Matrix4x4 RotationY(float radian) {
                float cosR = std::cosf(radian);
                float sinR = std::sinf(radian);
                return {
                    cosR, 0.0f, -sinR, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    sinR, 0.0f, cosR, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
            }

            Matrix4x4 RotationZ(float radian) {
                float cosR = std::cosf(radian);
                float sinR = std::sinf(radian);
                return {
                 cosR, sinR, 0.0f, 0.0f,
          -sinR, cosR, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
                };
            }

            Matrix4x4 MakeAffine(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
                float cosX = std::cosf(rotate.x);
                float sinX = std::sinf(rotate.x);
                float cosY = std::cosf(rotate.y);
                float sinY = std::sinf(rotate.y);
                float cosZ = std::cosf(rotate.z);
                float sinZ = std::sinf(rotate.z);

                return {
              scale.x * (cosY * cosZ),
              scale.x * (cosY * sinZ),
              scale.x * (-sinY),
               0.0f,

                scale.y * (sinX * sinY * cosZ - cosX * sinZ),
         scale.y * (sinX * sinY * sinZ + cosX * cosZ),
        scale.y * (sinX * cosY),
                  0.0f,

                    scale.z * (cosX * sinY * cosZ + sinX * sinZ),
             scale.z * (cosX * sinY * sinZ - sinX * cosZ),
           scale.z * (cosX * cosY),
         0.0f,

         translate.x, translate.y, translate.z, 1.0f
                };
            }

            Matrix4x4 MakeAffine(const Vector3& scale, const Quaternion& rotate, const Vector3& translate) {
                Matrix4x4 scaleMatrix = Scale(scale);
                Matrix4x4 rotateMatrix = QuaternionMath::MakeRotateMatrix(rotate);
                Matrix4x4 translateMatrix = Translation(translate);

                return Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
            }

        Matrix4x4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up) {
            Vector3 zAxis = Normalize(target - eye);
            Vector3 xAxis = Normalize(Cross(up, zAxis));
            Vector3 yAxis = Cross(zAxis, xAxis);

            return {
                xAxis.x, yAxis.x, zAxis.x, 0.0f,
                xAxis.y, yAxis.y, zAxis.y, 0.0f,
                xAxis.z, yAxis.z, zAxis.z, 0.0f,
                -Dot(xAxis, eye), -Dot(yAxis, eye), -Dot(zAxis, eye), 1.0f
            };
            }

        Matrix4x4 MakeRotateAxisAngle(const Vector3& axis, float radian) {
            // 0軸は単位行列
            if (Length(axis) == 0.0f) {
                return Identity();
            }

            const Vector3 n = Normalize(axis);

            // ---- ex を回す ----
            const Vector3 rx0 = { 1.0f, 0.0f, 0.0f };
            const Vector3 p0 = Vector::Project(rx0, n);
            const Vector3 a0 = rx0 - p0;
            const Vector3 b0 = Cross(n, a0);
            const Vector3 exp = p0 + (std::cosf(radian) * a0) + (std::sinf(radian) * b0);

            // ---- ey を回す ----
            const Vector3 ry0 = { 0.0f, 1.0f, 0.0f };
            const Vector3 p1 = Vector::Project(ry0, n);
            const Vector3 a1 = ry0 - p1;
            const Vector3 b1 = Cross(n, a1);
            const Vector3 eyp = p1 + (std::cosf(radian) * a1) + (std::sinf(radian) * b1);

            // ---- ez を回す ----
            const Vector3 rz0 = { 0.0f, 0.0f, 1.0f };
            const Vector3 p2 = Vector::Project(rz0, n);
            const Vector3 a2 = rz0 - p2;
            const Vector3 b2 = Cross(n, a2);
            const Vector3 ezp = p2 + (std::cosf(radian) * a2) + (std::sinf(radian) * b2);

                Matrix4x4 R{};
                R.m[0][0] = exp.x;
                R.m[0][1] = exp.y;
                R.m[0][2] = exp.z;
                R.m[0][3] = 0.0f;

                R.m[1][0] = eyp.x;
                R.m[1][1] = eyp.y;
                R.m[1][2] = eyp.z;
                R.m[1][3] = 0.0f;

                R.m[2][0] = ezp.x;
                R.m[2][1] = ezp.y;
                R.m[2][2] = ezp.z;
                R.m[2][3] = 0.0f;

                R.m[3][0] = 0.0f;
                R.m[3][1] = 0.0f;
                R.m[3][2] = 0.0f;
                R.m[3][3] = 1.0f;

                return R;
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
            translate = { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };

            scale.x = Length({ matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] });
            scale.y = Length({ matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] });
            scale.z = Length({ matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] });

                rotate.y = std::atan2(matrix.m[0][2], matrix.m[2][2]);
                rotate.x = std::atan2(-matrix.m[1][2], std::sqrt(matrix.m[1][0] * matrix.m[1][0] + matrix.m[1][1] * matrix.m[1][1]));
                rotate.z = std::atan2(matrix.m[1][0], matrix.m[1][1]);
            }
        }


        //================================================
        // クォータニオン演算の実装
        //================================================
        namespace QuaternionMath {
            Quaternion Multiply(const Quaternion& lhs, const Quaternion& rhs) {
                Quaternion result;

                // 実部の計算: w1*w2 から各虚部の内積を引く
                result.w = lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z;

                // i成分の計算: 実部同士の積 + 外積のi成分
                result.x = lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y;

                // j成分の計算: 実部同士の積 + 外積のj成分
                result.y = lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x;

                // k成分の計算: 実部同士の積 + 外積のk成分
                result.z = lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w;

                return result;
            }

            Quaternion Identity() {
                Quaternion result;

                result.x = 0.0f;  // i成分（X軸回転なし）
                result.y = 0.0f;  // j成分（Y軸回転なし） 
                result.z = 0.0f;  // k成分（Z軸回転なし）
                result.w = 1.0f;  // 実部（回転角度0を表す）

                return result;
            }

            Quaternion Conjugate(const Quaternion& q) {
                Quaternion result;

                // 共役クォータニオンの計算
                result.x = -q.x;  // i成分の符号を反転
                result.y = -q.y;  // j成分の符号を反転
                result.z = -q.z;  // k成分の符号を反転
                result.w = q.w;   // 実部はそのまま

                return result;
            }


            float Norm(const Quaternion& q) {
                return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            }

            Quaternion Normalize(const Quaternion& q) {
                float norm = QuaternionMath::Norm(q);
                assert(norm != 0.0f); // ゼロクォータニオンの正規化を防ぐ

                Quaternion result;
                result.x = q.x / norm;
                result.y = q.y / norm;
                result.z = q.z / norm;
                result.w = q.w / norm;

                return result;
            }

            Quaternion Inverse(const Quaternion& q) {
                float normSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
                assert(normSquared != 0.0f);

                Quaternion conjugate = QuaternionMath::Conjugate(q);
                return { conjugate.x / normSquared, conjugate.y / normSquared,
                        conjugate.z / normSquared, conjugate.w / normSquared };
            }

        Quaternion MakeRotateAxisAngle(const Vector3& axis, float radian) {
            // ゼロ軸の場合は単位クォータニオンを返す
            if (Length(axis) == 0.0f) {
                return Identity();
            }

            // 軸を正規化
            Vector3 normalizedAxis = Normalize(axis);

                // 半角の計算
                float halfAngle = radian * 0.5f;
                float sinHalf = std::sinf(halfAngle);
                float cosHalf = std::cosf(halfAngle);

                Quaternion result;
                // 虚部：sin(θ/2) * 正規化された軸
                result.x = normalizedAxis.x * sinHalf;
                result.y = normalizedAxis.y * sinHalf;
                result.z = normalizedAxis.z * sinHalf;
                // 実部：cos(θ/2)
                result.w = cosHalf;

                return result;
            }

            Vector3 RotateVector(const Vector3& vector, const Quaternion& quaternion) {
                // クォータニオンが正規化されていることを前提とした効率的な実装
                Vector3 qVec = { quaternion.x, quaternion.y, quaternion.z };  // 虚部
                float qw = quaternion.w;  // 実部

                // u × v を計算
                Vector3 cross1 = Vector::Cross(qVec, vector);

                // u × (u × v) を計算  
                Vector3 cross2 = Vector::Cross(qVec, cross1);

                // 最終結果：v + 2w(u × v) + 2(u × (u × v))
                Vector3 result;
                result.x = vector.x + 2.0f * qw * cross1.x + 2.0f * cross2.x;
                result.y = vector.y + 2.0f * qw * cross1.y + 2.0f * cross2.y;
                result.z = vector.z + 2.0f * qw * cross1.z + 2.0f * cross2.z;

                return result;
            }

            Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion) {
                Matrix4x4 result{};

                // 計算効率化のため事前に計算
                float x = quaternion.x;
                float y = quaternion.y;
                float z = quaternion.z;
                float w = quaternion.w;

                // よく使用される項を事前計算
                float xx = x * x;
                float yy = y * y;
                float zz = z * z;
                float xy = x * y;
                float xz = x * z;
                float yz = y * z;
                float wx = w * x;
                float wy = w * y;
                float wz = w * z;

                // 3x3回転部分の計算
                result.m[0][0] = 1.0f - 2.0f * (yy + zz);
                result.m[0][1] = 2.0f * (xy + wz);
                result.m[0][2] = 2.0f * (xz - wy);
                result.m[0][3] = 0.0f;

                result.m[1][0] = 2.0f * (xy - wz);
                result.m[1][1] = 1.0f - 2.0f * (xx + zz);
                result.m[1][2] = 2.0f * (yz + wx);
                result.m[1][3] = 0.0f;

                result.m[2][0] = 2.0f * (xz + wy);
                result.m[2][1] = 2.0f * (yz - wx);
                result.m[2][2] = 1.0f - 2.0f * (xx + yy);
                result.m[2][3] = 0.0f;

                // 同次座標部分（平行移動なし）
                result.m[3][0] = 0.0f;
                result.m[3][1] = 0.0f;
                result.m[3][2] = 0.0f;
                result.m[3][3] = 1.0f;

                return result;
            }

            Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t) {
                // クォータニオンの内積を計算
                float dot = q0.x * q1.x + q0.y * q1.y + q0.z * q1.z + q0.w * q1.w;

                // 内積が負の場合、q1を反転して最短経路を取る
                Quaternion q1Adjusted = q1;
                if (dot < 0.0f) {
                    dot = -dot;
                    q1Adjusted.x = -q1.x;
                    q1Adjusted.y = -q1.y;
                    q1Adjusted.z = -q1.z;
                    q1Adjusted.w = -q1.w;
                }

                // 内積がほぼ1の場合、線形補間で十分
                const float epsilon = 1e-6f;
                if (dot > 1.0f - epsilon) {
                    // 線形補間
                    Quaternion result;
                    result.x = (1.0f - t) * q0.x + t * q1Adjusted.x;
                    result.y = (1.0f - t) * q0.y + t * q1Adjusted.y;
                    result.z = (1.0f - t) * q0.z + t * q1Adjusted.z;
                    result.w = (1.0f - t) * q0.w + t * q1Adjusted.w;
                    return QuaternionMath::Normalize(result);
                }

                // θを計算
                float theta = std::acosf(dot);
                float sinTheta = std::sinf(theta);

                // スケーリング係数を計算
                float scale0 = std::sinf((1.0f - t) * theta) / sinTheta;
                float scale1 = std::sinf(t * theta) / sinTheta;

                // 補間結果を計算
                Quaternion result;
                result.x = scale0 * q0.x + scale1 * q1Adjusted.x;
                result.y = scale0 * q0.y + scale1 * q1Adjusted.y;
                result.z = scale0 * q0.z + scale1 * q1Adjusted.z;
                result.w = scale0 * q0.w + scale1 * q1Adjusted.w;
                return result;
            }
        }

        //================================================
        // 座標変換の実装
        //================================================
        namespace CoordinateTransform {
            Vector3 TransformCoord(const Vector3& vector, const Matrix4x4& matrix) {
                Vector3 result;
                result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + matrix.m[3][0];
                result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + matrix.m[3][1];
                result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + matrix.m[3][2];
                float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + matrix.m[3][3];

                result.x /= w;
                result.y /= w;
                result.z /= w;

                return result;
            }

            Vector4 TransformCoord(const Vector4& vector, const Matrix4x4& matrix) {
                Vector4 result;
                result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + vector.w * matrix.m[3][0];
                result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + vector.w * matrix.m[3][1];
                result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + vector.w * matrix.m[3][2];
                result.w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + vector.w * matrix.m[3][3];
                return result;
            }

            Vector3 TransformNormal(const Vector3& v, const Matrix4x4& m) {
                Vector3 result;
                result.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0];
                result.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1];
                result.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2];
                return result;
            }
        }

        //================================================
        // レンダリングパイプラインの実装
        //================================================
        namespace Rendering {
            Matrix4x4 PerspectiveFov(float fovY, float aspectRatio, float nearClip, float farClip) {
                return {
                    1.0f / (aspectRatio * std::tanf(fovY / 2.0f)), 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f / std::tanf(fovY / 2.0f), 0.0f, 0.0f,
                    0.0f, 0.0f, farClip / (farClip - nearClip), 1.0f,
                    0.0f, 0.0f, -nearClip * farClip / (farClip - nearClip), 0.0f
                };
            }

            Matrix4x4 Orthographic(float left, float top, float right, float bottom, float nearClip, float farClip) {
                return {
                    2.0f / (right - left), 0.0f, 0.0f, 0.0f,
                    0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f / (farClip - nearClip), 0.0f,
                    -(right + left) / (right - left), -(top + bottom) / (top - bottom), -nearClip / (farClip - nearClip), 1.0f
                };
            }

            Matrix4x4 Viewport(float left, float top, float width, float height, float minDepth, float maxDepth) {
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
                Matrix4x4 matVPV = Matrix::Multiply(Matrix::Multiply(viewMatrix, projectionMatrix),
                    Rendering::Viewport(0, 0, screenWidth, screenHeight, 0.0f, 1.0f));

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

                Matrix4x4 matVPV = Matrix::Multiply(Matrix::Multiply(viewMatrix, projectionMatrix),
                    Rendering::Viewport(0, 0, screenWidth, screenHeight, 0.0f, 1.0f));
                Matrix4x4 matInvVPV = Matrix::Inverse(matVPV);

                return CoordinateTransform::TransformCoord(screenCoord, matInvVPV);
            }

            Vector3 NormalizedScreenToWorldWithDepth(const Vector2& normalizedScreenPos, const Vector3& originalWorldPos,
                const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix,
                float screenWidth, float screenHeight) {
                float screenX = (normalizedScreenPos.x + 1.0f) * 0.5f * screenWidth;
                float screenY = (-normalizedScreenPos.y + 1.0f) * 0.5f * screenHeight;

                Matrix4x4 matVPV = Matrix::Multiply(Matrix::Multiply(viewMatrix, projectionMatrix),
                    Rendering::Viewport(0, 0, screenWidth, screenHeight, 0.0f, 1.0f));
                Vector3 originalScreenPos = CoordinateTransform::TransformCoord(originalWorldPos, matVPV);

                Vector3 targetScreenPos = { screenX, screenY, originalScreenPos.z };

                Matrix4x4 matInvVPV = Matrix::Inverse(matVPV);

                return CoordinateTransform::TransformCoord(targetScreenPos, matInvVPV);
            }
        }

    } // namespace MathCore
}
