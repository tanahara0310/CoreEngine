#pragma once
#include "Math/MathCore.h"
#include <cfloat>
#include <cmath>

/// @brief 軸対象境界ボックス（AABB）

namespace CoreEngine
{
struct BoundingBox {
    Vector3 min; ///< 最小座標
    Vector3 max; ///< 最大座標
    
    /// @brief デフォルトコンストラクタ（無効なボックス）
    BoundingBox() {
        min = { FLT_MAX, FLT_MAX, FLT_MAX };
        max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    }
    
    /// @brief コンストラクタ
    /// @param minPos 最小座標
    /// @param maxPos 最大座標
    BoundingBox(const Vector3& minPos, const Vector3& maxPos) {
        min = minPos;
        max = maxPos;
    }

    /// @brief 有効なボックスかチェック
    /// @return 有効な場合true
    bool IsValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
    
    /// @brief 中心点を取得
    /// @return 中心座標
    Vector3 GetCenter() const {
        return Vector3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f);
    }
    
    /// @brief サイズを取得
    /// @return ボックスサイズ
    Vector3 GetSize() const {
        return Vector3(max.x - min.x, max.y - min.y, max.z - min.z);
    }

    void SetBoundingBox(const Vector3& center, const Vector3& size) {
        Vector3 half = size * 0.5f;
        min = center - half;
        max = center + half;
    }

    /// @brief アフィン行列（行ベクトル規約: p' = p * M）で変換した外接AABBを返す
    /// @param worldMatrix ワールド行列
    /// @return 変換後のAABB（自身が無効な場合は無効なAABB）
    BoundingBox TransformBy(const Matrix4x4& worldMatrix) const {
        if (!IsValid()) {
            return BoundingBox();
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

        return BoundingBox(
            Vector3{ wx - rx, wy - ry, wz - rz },
            Vector3{ wx + rx, wy + ry, wz + rz });
    }
};
}
