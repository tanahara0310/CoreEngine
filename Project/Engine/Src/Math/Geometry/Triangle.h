#pragma once

#include "Math/Geometry/Shapes.h"

#include <cstdint>

/// @file
/// @brief 三角形 1 つに対する最近接点と交差

namespace CoreEngine
{
namespace Geometry
{
    /// @brief 三角形の上のどこにいちばん近い点があるか
    /// @details 辺 0 は a-b、辺 1 は b-c、辺 2 は c-a。頂点 0・1・2 は a・b・c。
    enum class TriangleFeature : uint8_t {
        Face,
        Edge0,
        Edge1,
        Edge2,
        Vertex0,
        Vertex1,
        Vertex2,
    };

    /// @brief 三角形の上の点と、その点がある場所
    struct TrianglePoint {
        Vector3         point{ 0.0f, 0.0f, 0.0f };
        TriangleFeature feature = TriangleFeature::Face;
    };

    /// @brief 点 p に最も近い三角形 abc の上の点
    TrianglePoint ClosestPointOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c);

    /// @brief 線分 p0-p1 が三角形 abc を貫くか
    /// @param outPoint 貫く点の書き出し先
    bool SegmentIntersectsTriangle(const Vector3& p0, const Vector3& p1,
                                   const Vector3& a, const Vector3& b, const Vector3& c,
                                   Vector3& outPoint);

    /// @brief 線分と三角形の最も近い 2 点
    /// @param outOnSegment 線分の上の点
    /// @param outOnTriangle 三角形の上の点（場所付き）
    /// @return 線分が三角形を貫いていれば true（このとき 2 点は同じ点）
    bool ClosestPointsSegmentTriangle(const Vector3& p0, const Vector3& p1,
                                      const Vector3& a, const Vector3& b, const Vector3& c,
                                      Vector3& outOnSegment, TrianglePoint& outOnTriangle);
}
}
