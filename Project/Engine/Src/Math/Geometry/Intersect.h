#pragma once

#include "Math/Geometry/Shapes.h"

/// @file
/// @brief 形状同士の交差判定
/// @details **1 つの形状ペアにつき実装は 1 箇所だけ**。対称なペア（AABB×Sphere など）は
///          引数を入れ替えて転送し、法線だけ反転する。以前は球×AABB の判定が
///          SphereCollider と AABBCollider の両方に書かれており、片方だけ直す事故が
///          起きる構造だった。

namespace CoreEngine
{
namespace Geometry
{
    /// @brief 接触情報
    /// @details **符号規約: normal は A から B へ向かう向き。**
    ///          B を normal 方向へ depth だけ動かすと接触が解ける。
    ///          引数の順序で向きが決まるので、転送する側は必ず法線を反転すること。
    struct Contact {
        Vector3 normal{ 0.0f, 0.0f, 0.0f };  ///< A→B の押し出し方向（正規化済み）
        float   depth = 0.0f;                ///< 貫通深度（0 以上）
        Vector3 point{ 0.0f, 0.0f, 0.0f };   ///< 代表接触点
    };

    /// @brief 1 軸上の重なり量と押し出し向き
    struct AxisOverlap {
        float depth = 0.0f;      ///< 重なり量（0 以上）
        float direction = 0.0f;  ///< a を離す向き（-1 or +1）
    };

    /// @brief 1 軸上で 2 区間の重なりを求める
    /// @param aMin,aMax 区間 A / @param bMin,bMax 区間 B
    /// @param out a を direction 方向へ depth 動かすと分離する
    /// @return 重なっていれば true（接しているだけなら depth = 0 で true）
    /// @details AABB×AABB の接触情報と、2D タイルの軸別押し出し（TileCollider）が
    ///          同じ式を使うための共通土台。押し出し量の計算を 2 箇所に散らさない。
    bool OverlapOnAxis(float aMin, float aMax, float bMin, float bMax, AxisOverlap& out);

    //================================================
    // 点の内包判定
    //================================================

    bool Contains(const AABB& box, const Vector3& point);
    bool Contains(const Sphere& sphere, const Vector3& point);
    bool Contains(const Capsule& capsule, const Vector3& point);

    //================================================
    // 形状同士の交差判定
    //================================================
    /// @param outContact 省略可。nullptr なら接触情報を計算しない高速パスを通る。

    bool Intersect(const Sphere& a, const Sphere& b, Contact* outContact = nullptr);
    bool Intersect(const AABB& a, const AABB& b, Contact* outContact = nullptr);

    /// @note 実装はこちら（球が A）。AABB×Sphere はこれへ転送する。
    bool Intersect(const Sphere& sphere, const AABB& box, Contact* outContact = nullptr);
    bool Intersect(const AABB& box, const Sphere& sphere, Contact* outContact = nullptr);

    /// @note 実装はこちら（カプセルが A）。Sphere×Capsule はこれへ転送する。
    bool Intersect(const Capsule& capsule, const Sphere& sphere, Contact* outContact = nullptr);
    bool Intersect(const Sphere& sphere, const Capsule& capsule, Contact* outContact = nullptr);

    bool Intersect(const Capsule& a, const Capsule& b, Contact* outContact = nullptr);
}
}
