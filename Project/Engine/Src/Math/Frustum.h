#pragma once
#include "Math/MathCore.h"
#include "Math/Geometry/Shapes.h"
#include <array>

/// @brief 視錐台（Frustum）構造体 - カメラの可視領域を6平面で表現

namespace CoreEngine
{

/// @brief 平面（法線 + 距離）
/// @note 実体は Geometry::Plane。以前はここと CollisionUtils に符号規約の違う Plane が
///       2 つあり、取り違えると符号が反転するバグになっていたので一本化した。
///       規約: dot(normal, p) + d = 0 が平面上。DistanceTo() が正なら法線側。
///       視錐台では法線が内側を向くので、正 = 内側。
using Plane = Geometry::Plane;

/// @brief 視錐台の平面インデックス
enum class FrustumPlane {
    Left = 0,
    Right,
    Bottom,
    Top,
    Near,
    Far,
    Count
};

/// @brief 視錐台（6平面で構成）
struct Frustum {
    std::array<Plane, static_cast<size_t>(FrustumPlane::Count)> planes;

    /// @brief VP行列（View * Projection）から6平面を抽出する（Gribb-Hartmann法）
    /// @param vp ビュー × プロジェクション行列
    void ExtractFromMatrix(const Matrix4x4& vp) {
        // Left:   row3 + row0
        planes[0].normal.x = vp.m[0][3] + vp.m[0][0];
        planes[0].normal.y = vp.m[1][3] + vp.m[1][0];
        planes[0].normal.z = vp.m[2][3] + vp.m[2][0];
        planes[0].d        = vp.m[3][3] + vp.m[3][0];

        // Right:  row3 - row0
        planes[1].normal.x = vp.m[0][3] - vp.m[0][0];
        planes[1].normal.y = vp.m[1][3] - vp.m[1][0];
        planes[1].normal.z = vp.m[2][3] - vp.m[2][0];
        planes[1].d        = vp.m[3][3] - vp.m[3][0];

        // Bottom: row3 + row1
        planes[2].normal.x = vp.m[0][3] + vp.m[0][1];
        planes[2].normal.y = vp.m[1][3] + vp.m[1][1];
        planes[2].normal.z = vp.m[2][3] + vp.m[2][1];
        planes[2].d        = vp.m[3][3] + vp.m[3][1];

        // Top:    row3 - row1
        planes[3].normal.x = vp.m[0][3] - vp.m[0][1];
        planes[3].normal.y = vp.m[1][3] - vp.m[1][1];
        planes[3].normal.z = vp.m[2][3] - vp.m[2][1];
        planes[3].d        = vp.m[3][3] - vp.m[3][1];

        // Near:   row3 + row2
        planes[4].normal.x = vp.m[0][3] + vp.m[0][2];
        planes[4].normal.y = vp.m[1][3] + vp.m[1][2];
        planes[4].normal.z = vp.m[2][3] + vp.m[2][2];
        planes[4].d        = vp.m[3][3] + vp.m[3][2];

        // Far:    row3 - row2
        planes[5].normal.x = vp.m[0][3] - vp.m[0][2];
        planes[5].normal.y = vp.m[1][3] - vp.m[1][2];
        planes[5].normal.z = vp.m[2][3] - vp.m[2][2];
        planes[5].d        = vp.m[3][3] - vp.m[3][2];

        // 各平面を正規化
        for (auto& plane : planes) {
            float length = Length(plane.normal);
            if (length > 0.0f) {
                plane.normal.x /= length;
                plane.normal.y /= length;
                plane.normal.z /= length;
                plane.d /= length;
            }
        }
    }

    /// @brief AABBが視錐台の完全に外側にあるか判定
    /// @param aabb 判定対象のバウンディングボックス（ワールド空間）
    /// @return 完全に外側なら true（描画スキップ可能）
    bool IsOutside(const BoundingBox& aabb) const {
        for (const auto& plane : planes) {
            // P-vertex: 法線方向に最も遠い頂点（最も内側にある可能性が高い点）
            Vector3 pVertex = {
                plane.normal.x > 0.0f ? aabb.max.x : aabb.min.x,
                plane.normal.y > 0.0f ? aabb.max.y : aabb.min.y,
                plane.normal.z > 0.0f ? aabb.max.z : aabb.min.z
            };

            // P-vertex が平面の外側 → AABBは完全にこの平面の外
            if (plane.DistanceTo(pVertex) < 0.0f) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace CoreEngine
