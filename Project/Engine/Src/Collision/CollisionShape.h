#pragma once

#include "Math/Geometry/Shapes.h"

/// @file
/// @brief コライダーの形状記述（ローカル空間）
/// @details 「どんな形か」だけを持つデータ。ワールド空間の形状（Geometry::Sphere 等）は
///          オーナーの Transform を適用して Collider が生成する。

namespace CoreEngine
{
    /// @brief コライダーの形状種別
    /// @note 追加するときは Collider の判定ディスパッチ表も必ず埋めること
    ///       （表のサイズが合わなくなるのでコンパイルエラーで気づける）。
    enum class ColliderShapeType {
        Sphere,   ///< 球
        Box,      ///< 軸平行ボックス（回転は未対応）
        Count,
    };

    /// @brief コライダーの形状（オーナー原点を基準としたローカル定義）
    /// @details 実際の判定サイズにはオーナーの GetWorldScale() が乗る。
    ///          カプセルは Geometry 側に交差判定はあるが AABB×Capsule が未実装のため
    ///          コライダーの形状としてはまだ選べない（Phase 3 の申し送り事項）。
    struct CollisionShape {
        ColliderShapeType type = ColliderShapeType::Sphere;

        /// @brief オーナー原点からのローカルオフセット
        /// @note スケール適用後にワールドへ足す。1 オブジェクトに複数コライダーを
        ///       置くときの配置に使う（頭・胴など）。
        Vector3 offset{ 0.0f, 0.0f, 0.0f };

        /// @brief 球の半径（type == Sphere のとき有効）
        float radius = 0.5f;

        /// @brief ボックスの各軸サイズ（type == Box のとき有効）
        Vector3 size{ 1.0f, 1.0f, 1.0f };

        /// @brief 球形状を作る
        static CollisionShape MakeSphere(float radius, const Vector3& offset = {}) {
            CollisionShape shape;
            shape.type = ColliderShapeType::Sphere;
            shape.radius = radius;
            shape.offset = offset;
            return shape;
        }

        /// @brief ボックス形状を作る
        static CollisionShape MakeBox(const Vector3& size, const Vector3& offset = {}) {
            CollisionShape shape;
            shape.type = ColliderShapeType::Box;
            shape.size = size;
            shape.offset = offset;
            return shape;
        }
    };
}
