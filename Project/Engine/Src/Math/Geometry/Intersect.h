#pragma once

#include "Math/Geometry/Shapes.h"

/// @file
/// @brief 形状同士の交差判定
/// @note 1 つの形状ペアにつき実装は 1 箇所だけ。対称なペアは引数を入れ替えて転送し、法線だけ反転する。

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
    /// @param out a を direction 方向へ depth 動かすと分離する
    /// @return 重なっていれば true（接しているだけなら depth = 0 で true）
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

    /// @note 実装はこちら（球が A）。OBB×Sphere はこれへ転送する。
    bool Intersect(const Sphere& sphere, const OBB& box, Contact* outContact = nullptr);
    bool Intersect(const OBB& box, const Sphere& sphere, Contact* outContact = nullptr);

    /// @note 15 本の分離軸を調べ、重なりが最小の軸を押し出し方向にする。
    bool Intersect(const OBB& a, const OBB& b, Contact* outContact = nullptr);

    /// @brief 2 つの箱が触れている面の接触点を集める
    /// @param a         箱その 1
    /// @param b         箱その 2
    /// @param normal    交差判定が返した法線（a から b へ向かう）
    /// @param outPoints 接触点の書き出し先。normal は引数のものをそのまま入れる
    /// @param maxPoints 書き出せる数の上限
    /// @return 集まった点の数。0 なら面ではなく辺や角で触れているので、代表点 1 つで扱うこと
    /// @details 相手の内部に入っている頂点を接触点とする。深さは法線方向に測り直すので、
    ///          傾いて触れている面では点ごとに違う値になる。
    int CollectBoxContacts(const OBB& a, const OBB& b, const Vector3& normal,
                           Contact* outPoints, int maxPoints);

    /// @note 実装はこちら（カプセルが A）。Sphere×Capsule はこれへ転送する。
    bool Intersect(const Capsule& capsule, const Sphere& sphere, Contact* outContact = nullptr);
    bool Intersect(const Sphere& sphere, const Capsule& capsule, Contact* outContact = nullptr);

    bool Intersect(const Capsule& a, const Capsule& b, Contact* outContact = nullptr);

    /// @note 実装はこちら（カプセルが A）。OBB×Capsule はこれへ転送する。
    /// @details 線分上で箱にいちばん近い点を反復で求め、そこを中心とする球として解く。
    bool Intersect(const Capsule& capsule, const OBB& box, Contact* outContact = nullptr);
    bool Intersect(const OBB& box, const Capsule& capsule, Contact* outContact = nullptr);

    /// @note 箱を向きの無い OBB として扱い、Capsule×OBB へ転送する。
    bool Intersect(const Capsule& capsule, const AABB& box, Contact* outContact = nullptr);
    bool Intersect(const AABB& box, const Capsule& capsule, Contact* outContact = nullptr);
}
}
