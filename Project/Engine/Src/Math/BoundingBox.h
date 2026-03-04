#pragma once
#include "Math/MathCore.h"
#include <cfloat>

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
};
}
