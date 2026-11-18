#include "pch.h"
#include "Math/Geometry/Triangle.h"
#include "Math/Geometry/Distance.h"

#include <cmath>

namespace CoreEngine
{
namespace Geometry
{
    TrianglePoint ClosestPointOnTriangle(const Vector3& p, const Vector3& a, const Vector3& b, const Vector3& c)
    {
        // 頂点・辺・面の領域を順に調べ、p がどの領域にあるかで点を決める
        const Vector3 ab = b - a;
        const Vector3 ac = c - a;
        const Vector3 ap = p - a;

        const float d1 = Dot(ab, ap);
        const float d2 = Dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) {
            return { a, TriangleFeature::Vertex0 };
        }

        const Vector3 bp = p - b;
        const float d3 = Dot(ab, bp);
        const float d4 = Dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) {
            return { b, TriangleFeature::Vertex1 };
        }

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            const float v = d1 / (d1 - d3);
            return { a + ab * v, TriangleFeature::Edge0 };
        }

        const Vector3 cp = p - c;
        const float d5 = Dot(ab, cp);
        const float d6 = Dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) {
            return { c, TriangleFeature::Vertex2 };
        }

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            const float w = d2 / (d2 - d6);
            return { a + ac * w, TriangleFeature::Edge2 };
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return { b + (c - b) * w, TriangleFeature::Edge1 };
        }

        const float denominator = va + vb + vc;
        if (!(std::abs(denominator) > 0.0f)) {
            return { a, TriangleFeature::Vertex0 };
        }
        const float inverse = 1.0f / denominator;
        const float v = vb * inverse;
        const float w = vc * inverse;
        return { a + ab * v + ac * w, TriangleFeature::Face };
    }

    bool SegmentIntersectsTriangle(const Vector3& p0, const Vector3& p1,
                                   const Vector3& a, const Vector3& b, const Vector3& c,
                                   Vector3& outPoint)
    {
        constexpr float kParallelEpsilon = 1e-12f;

        const Vector3 direction = p1 - p0;
        const Vector3 edge1 = b - a;
        const Vector3 edge2 = c - a;
        const Vector3 h = Cross(direction, edge2);
        const float det = Dot(edge1, h);
        if (std::abs(det) < kParallelEpsilon) {
            return false;
        }

        const float inverse = 1.0f / det;
        const Vector3 s = p0 - a;
        const float u = inverse * Dot(s, h);
        if (u < 0.0f || u > 1.0f) {
            return false;
        }
        const Vector3 q = Cross(s, edge1);
        const float v = inverse * Dot(direction, q);
        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }
        const float t = inverse * Dot(edge2, q);
        if (t < 0.0f || t > 1.0f) {
            return false;
        }
        outPoint = p0 + direction * t;
        return true;
    }

    bool ClosestPointsSegmentTriangle(const Vector3& p0, const Vector3& p1,
                                      const Vector3& a, const Vector3& b, const Vector3& c,
                                      Vector3& outOnSegment, TrianglePoint& outOnTriangle)
    {
        Vector3 crossing{};
        if (SegmentIntersectsTriangle(p0, p1, a, b, c, crossing)) {
            outOnSegment = crossing;
            outOnTriangle = { crossing, TriangleFeature::Face };
            return true;
        }

        // 貫かないときの最も近い組は、線分の端と三角形、または線分と三角形の辺のどれか
        float bestSq = FLT_MAX;
        const auto consider = [&](const Vector3& onSegment, const TrianglePoint& onTriangle) {
            const Vector3 delta = onSegment - onTriangle.point;
            const float distanceSq = Dot(delta, delta);
            if (distanceSq < bestSq) {
                bestSq = distanceSq;
                outOnSegment = onSegment;
                outOnTriangle = onTriangle;
            }
        };

        consider(p0, ClosestPointOnTriangle(p0, a, b, c));
        consider(p1, ClosestPointOnTriangle(p1, a, b, c));

        const LineSegment segment(p0, p1);
        const Vector3 corners[3] = { a, b, c };
        for (int edge = 0; edge < 3; ++edge) {
            Vector3 onSegment{}, onEdge{};
            ClosestPointsBetweenSegments(segment, LineSegment(corners[edge], corners[(edge + 1) % 3]),
                onSegment, onEdge);
            // 辺の上の点の場所は、三角形の最近接点で決め直す（辺の端なら頂点になる）
            consider(onSegment, ClosestPointOnTriangle(onSegment, a, b, c));
        }
        return false;
    }
}
}
