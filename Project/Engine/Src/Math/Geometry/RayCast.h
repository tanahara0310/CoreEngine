#pragma once

#include "Math/Geometry/Shapes.h"
#include <limits>

/// @file
/// @brief レイと形状の交差判定
/// @details エディタのピッキングもゲームロジックのレイキャストもここを使う。
///          以前は ObjectSelector が Ray×Sphere / Ray×AABB を独自に再実装していた。

namespace CoreEngine
{
namespace Geometry
{
    /// @brief レイ交差の結果
    /// @note distance は **ray.direction の長さを単位とする** パラメータ t。
    ///       direction を正規化して渡していればワールド距離と一致する。
    ///       ローカル空間へ変換した非正規化レイでも t の大小関係は保たれるので、
    ///       同一オブジェクト内の比較にはそのまま使える。
    struct RayHit {
        float   distance = 0.0f;             ///< 交点までの t
        Vector3 point{ 0.0f, 0.0f, 0.0f };   ///< 交点
        Vector3 normal{ 0.0f, 0.0f, 0.0f };  ///< 交点の法線（レイ側を向く）
    };

    /// @brief t の既定上限
    inline constexpr float kRayMaxDistance = (std::numeric_limits<float>::max)();

    /// @param outHit 省略可。nullptr なら交点・法線を計算しない高速パスを通る。
    /// @param tMin   これより手前のヒットは無視する
    /// @param tMax   これより奥のヒットは無視する
    /// @note レイ原点が形状の内部にある場合は、入口ではなく出口（tMin 以降で最初のヒット）を返す。

    bool Raycast(const Ray& ray, const Sphere& sphere, RayHit* outHit = nullptr,
                 float tMin = 0.0f, float tMax = kRayMaxDistance);

    bool Raycast(const Ray& ray, const AABB& box, RayHit* outHit = nullptr,
                 float tMin = 0.0f, float tMax = kRayMaxDistance);

    bool Raycast(const Ray& ray, const Plane& plane, RayHit* outHit = nullptr,
                 float tMin = 0.0f, float tMax = kRayMaxDistance);

    /// @brief レイと三角形（Möller–Trumbore）
    /// @note 表裏どちらからでもヒットする
    bool RaycastTriangle(const Ray& ray,
                         const Vector3& v0, const Vector3& v1, const Vector3& v2,
                         RayHit* outHit = nullptr,
                         float tMin = 0.0f, float tMax = kRayMaxDistance);
}
}
