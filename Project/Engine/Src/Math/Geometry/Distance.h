#pragma once

#include "Math/Geometry/Shapes.h"

/// @file
/// @brief 距離・最近接点の計算
/// @details 交差判定（Intersect.h）はここの結果を使って書く。同じ計算を
///          二度書かないための土台。

namespace CoreEngine
{
    namespace Geometry
    {
        //================================================
        // 最近接点
        //================================================

        /// @brief 点から線分への最近接点
        Vector3 ClosestPointOnSegment(const Vector3& point, const LineSegment& segment);

        /// @brief 点から平面への最近接点
        Vector3 ClosestPointOnPlane(const Vector3& point, const Plane& plane);

        /// @brief 点から AABB への最近接点（内部の点はその点自身）
        Vector3 ClosestPointOnAABB(const Vector3& point, const AABB& box);

        /// @brief 点から球の表面への最近接点
        /// @note 点が中心と一致する場合は +X 方向の表面上の点を返す
        Vector3 ClosestPointOnSphere(const Vector3& point, const Sphere& sphere);

        /// @brief 2 線分間の最近接点ペア
        /// @param a         線分 A
        /// @param b         線分 B
        /// @param outPointA A 上の最近接点
        /// @param outPointB B 上の最近接点
        /// @note 縮退（長さ 0）にも対応する。カプセル同士の判定はこれを土台にする。
        void ClosestPointsBetweenSegments(const LineSegment& a, const LineSegment& b,
            Vector3& outPointA, Vector3& outPointB);

        //================================================
        // 距離
        //================================================

        /// @brief 点と点の距離
        float DistancePointToPoint(const Vector3& a, const Vector3& b);

        /// @brief 点と平面の符号付き距離（Plane::DistanceTo と同じ）
        float DistancePointToPlane(const Vector3& point, const Plane& plane);

        /// @brief 点と線分の最短距離
        float DistancePointToSegment(const Vector3& point, const LineSegment& segment);

        /// @brief 点と球表面の距離（内部なら負）
        float DistancePointToSphere(const Vector3& point, const Sphere& sphere);

        /// @brief 点と AABB の距離（内部なら 0）
        float DistancePointToAABB(const Vector3& point, const AABB& box);
    }
}
