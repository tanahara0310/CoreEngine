#pragma once

#include "Math/MathCore.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>

/// @file
/// @brief 幾何形状の値型
/// @details 純粋幾何レイヤ。GameObject・レンダラ・ImGui に依存しない。
///          交差判定は Intersect.h、距離・最近接点は Distance.h、
///          レイ判定は RayCast.h にある。
///          **形状の定義はすべてこのファイルに集約する**（以前は AABB だけ
///          Math/BoundingBox.h に独立していて置き場所が不揃いだった）。

namespace CoreEngine
{
namespace Geometry
{
    /// @brief 軸平行境界ボックス（AABB）
    /// @note 既定コンストラクタは無効なボックス（min > max）。IsValid() で検出できる。
    struct AABB {
        Vector3 min; ///< 最小座標
        Vector3 max; ///< 最大座標

        /// @brief 既定コンストラクタ（無効なボックス）
        AABB() {
            min = { FLT_MAX, FLT_MAX, FLT_MAX };
            max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        }

        AABB(const Vector3& minPos, const Vector3& maxPos) {
            min = minPos;
            max = maxPos;
        }

        /// @brief 有効なボックスかチェック
        bool IsValid() const {
            return min.x <= max.x && min.y <= max.y && min.z <= max.z;
        }

        /// @brief 中心点を取得
        Vector3 GetCenter() const {
            return Vector3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f);
        }

        /// @brief サイズを取得
        Vector3 GetSize() const {
            return Vector3(max.x - min.x, max.y - min.y, max.z - min.z);
        }

        /// @brief 中心とサイズから設定する
        void SetBoundingBox(const Vector3& center, const Vector3& size) {
            Vector3 half = size * 0.5f;
            min = center - half;
            max = center + half;
        }

        /// @brief アフィン行列（行ベクトル規約: p' = p * M）で変換した外接AABBを返す
        /// @param worldMatrix ワールド行列
        /// @return 変換後のAABB（自身が無効な場合は無効なAABB）
        AABB TransformBy(const Matrix4x4& worldMatrix) const {
            if (!IsValid()) {
                return AABB();
            }
            // 中心 + 拡張の変換（8頂点変換と等価でより少ない演算量）
            const auto& m = worldMatrix.m;
            const float cx = (min.x + max.x) * 0.5f;
            const float cy = (min.y + max.y) * 0.5f;
            const float cz = (min.z + max.z) * 0.5f;
            const float ex = (max.x - min.x) * 0.5f;
            const float ey = (max.y - min.y) * 0.5f;
            const float ez = (max.z - min.z) * 0.5f;

            const float wx = cx * m[0][0] + cy * m[1][0] + cz * m[2][0] + m[3][0];
            const float wy = cx * m[0][1] + cy * m[1][1] + cz * m[2][1] + m[3][1];
            const float wz = cx * m[0][2] + cy * m[1][2] + cz * m[2][2] + m[3][2];

            const float rx = ex * std::abs(m[0][0]) + ey * std::abs(m[1][0]) + ez * std::abs(m[2][0]);
            const float ry = ex * std::abs(m[0][1]) + ey * std::abs(m[1][1]) + ez * std::abs(m[2][1]);
            const float rz = ex * std::abs(m[0][2]) + ey * std::abs(m[1][2]) + ez * std::abs(m[2][2]);

            return AABB(
                Vector3{ wx - rx, wy - ry, wz - rz },
                Vector3{ wx + rx, wy + ry, wz + rz });
        }

        /// @brief 全方向へ一様に拡張したボックスを返す
        AABB Expanded(float expansion) const {
            const Vector3 e{ expansion, expansion, expansion };
            return AABB(min - e, max + e);
        }

        /// @brief 軸ごとの量で拡張したボックスを返す
        AABB Expanded(const Vector3& expansion) const {
            return AABB(min - expansion, max + expansion);
        }

        /// @brief 点を含むように拡張したボックスを返す
        AABB Including(const Vector3& point) const {
            return AABB(
                Vector3{ (std::min)(min.x, point.x), (std::min)(min.y, point.y), (std::min)(min.z, point.z) },
                Vector3{ (std::max)(max.x, point.x), (std::max)(max.y, point.y), (std::max)(max.z, point.z) });
        }

        /// @brief 点群を包む AABB を作る
        /// @return 点が 0 個なら無効な AABB
        static AABB FromPoints(const Vector3* points, size_t count) {
            if (points == nullptr || count == 0) {
                return AABB();
            }
            AABB result(points[0], points[0]);
            for (size_t i = 1; i < count; ++i) {
                result = result.Including(points[i]);
            }
            return result;
        }
    };

    /// @brief 球
    /// @note 既定コンストラクタは半径 0 の縮退形状（何とも衝突しない）。
    ///       BoundingBox が無効値から始まるのと同じ方針で、値の設定漏れが
    ///       「それっぽく動いてしまう」のを防ぐ。
    struct Sphere {
        Vector3 center;
        float   radius;

        Sphere() : center({ 0.0f, 0.0f, 0.0f }), radius(0.0f) {}
        Sphere(const Vector3& center, float radius) : center(center), radius(radius) {}
    };

    /// @brief カプセル（線分 + 半径）
    /// @note 既定コンストラクタは長さ 0・半径 0 の縮退形状。
    struct Capsule {
        Vector3 start;
        Vector3 end;
        float   radius;

        Capsule() : start({ 0.0f, 0.0f, 0.0f }), end({ 0.0f, 0.0f, 0.0f }), radius(0.0f) {}
        Capsule(const Vector3& start, const Vector3& end, float radius)
            : start(start), end(end), radius(radius) {}
    };

    /// @brief 線分
    struct LineSegment {
        Vector3 start;
        Vector3 end;

        LineSegment() : start({ 0.0f, 0.0f, 0.0f }), end({ 0.0f, 0.0f, 0.0f }) {}
        LineSegment(const Vector3& start, const Vector3& end) : start(start), end(end) {}
    };

    /// @brief レイ（半直線）
    /// @note direction は正規化して渡すこと。RayHit::distance がワールド距離になる。
    struct Ray {
        Vector3 origin;
        Vector3 direction;

        Ray() : origin({ 0.0f, 0.0f, 0.0f }), direction({ 0.0f, 0.0f, 1.0f }) {}
        Ray(const Vector3& origin, const Vector3& direction) : origin(origin), direction(direction) {}
    };

    /// @brief 平面
    /// @details **符号規約: dot(normal, p) + d = 0 が平面上。DistanceTo() が正なら法線側（表）。**
    ///          以前は Frustum.h（normal, d）と CollisionUtils（normal, distance で符号が逆）に
    ///          別々の Plane があり、取り違えると符号が反転するバグになっていた。ここに一本化する。
    struct Plane {
        Vector3 normal;  ///< 正規化済み法線
        float   d;       ///< 原点から法線方向への符号付き距離の符号反転値

        Plane() : normal({ 0.0f, 1.0f, 0.0f }), d(0.0f) {}
        Plane(const Vector3& normal, float d) : normal(normal), d(d) {}

        /// @brief 点と法線から平面を作る
        static Plane FromPointNormal(const Vector3& point, const Vector3& normal) {
            const Vector3 n = ::CoreEngine::Normalize(normal);
            return Plane(n, -::CoreEngine::Dot(n, point));
        }

        /// @brief 点との符号付き距離
        /// @return 正なら法線側（表）、負なら裏、0 なら平面上
        float DistanceTo(const Vector3& point) const {
            return ::CoreEngine::Dot(normal, point) + d;
        }
    };
}

/// @brief AABB の別名（描画・カリング側は歴史的に BoundingBox と呼んでいる）
/// @note 実体は Geometry::AABB。既存コードを一斉改名しないための互換名で、
///       新規コードでは Geometry::AABB を使うこと。
using BoundingBox = Geometry::AABB;
}
