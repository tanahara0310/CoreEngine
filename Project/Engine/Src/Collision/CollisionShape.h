#pragma once

#include "Math/Geometry/Shapes.h"

#include <cstddef>
#include <iterator>

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
        Sphere,    ///< 球
        Box,       ///< ボックス（オーナーの向きに合わせて回る）
        Capsule,   ///< カプセル（オーナーの上方向に立つ）
        Count,
    };

    /// @brief 形状種別の名前（列挙の並びと同じ順。保存データが使う）
    inline constexpr const char* kColliderShapeTypeNames[] = { "Sphere", "Box", "Capsule" };
    static_assert(std::size(kColliderShapeTypeNames) == static_cast<std::size_t>(ColliderShapeType::Count),
        "ColliderShapeType を増減したら kColliderShapeTypeNames も更新すること");

    /// @brief コライダーの形状（オーナー原点を基準としたローカル定義）
    /// @details 実際の判定サイズにはオーナーの GetWorldScale() が乗る。
    struct CollisionShape {
        ColliderShapeType type = ColliderShapeType::Sphere;

        /// @brief オーナー原点からのローカルオフセット
        /// @note スケール適用後にワールドへ足す。1 オブジェクトに複数コライダーを
        ///       置くときの配置に使う（頭・胴など）。
        Vector3 offset{ 0.0f, 0.0f, 0.0f };

        /// @brief 球とカプセルの半径（type == Sphere / Capsule のとき有効）
        float radius = 0.5f;

        /// @brief ボックスの各軸サイズ（type == Box のとき有効）
        Vector3 size{ 1.0f, 1.0f, 1.0f };

        /// @brief カプセルの全高（type == Capsule のとき有効。両端の半球を含む）
        /// @note 2 * radius を下回る指定は球と同じ形になる。
        float height = 2.0f;

        /// @brief 半球を除いた円筒部分の長さ
        float CylinderLength() const {
            const float length = height - radius * 2.0f;
            return (length > 0.0f) ? length : 0.0f;
        }

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

        /// @brief カプセル形状を作る
        /// @param height 両端の半球を含む全高
        static CollisionShape MakeCapsule(float radius, float height, const Vector3& offset = {}) {
            CollisionShape shape;
            shape.type = ColliderShapeType::Capsule;
            shape.radius = radius;
            shape.height = height;
            shape.offset = offset;
            return shape;
        }
    };
}
