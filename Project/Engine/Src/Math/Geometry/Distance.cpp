#include "pch.h"
#include "Math/Geometry/Distance.h"

#include <cmath>

namespace CoreEngine
{
namespace Geometry
{
    namespace {
        /// @brief 縮退（長さ 0）とみなす閾値の二乗
        constexpr float kDegenerateLengthSq = 1e-12f;
    }

    //================================================
    // 最近接点
    //================================================

    Vector3 ClosestPointOnSegment(const Vector3& point, const LineSegment& segment)
    {
        const Vector3 segmentVec = segment.end - segment.start;
        const float lengthSq = Dot(segmentVec, segmentVec);
        if (lengthSq < kDegenerateLengthSq) {
            return segment.start;   // 長さ 0 の線分は点として扱う
        }

        float t = Dot(point - segment.start, segmentVec) / lengthSq;
        t = MathCore::Clamp(t, 0.0f, 1.0f);

        return segment.start + segmentVec * t;
    }

    Vector3 ClosestPointOnPlane(const Vector3& point, const Plane& plane)
    {
        return point - plane.normal * plane.DistanceTo(point);
    }

    Vector3 ClosestPointOnAABB(const Vector3& point, const AABB& box)
    {
        return MathCore::Clamp(point, box.min, box.max);
    }

    Vector3 ClosestPointOnSphere(const Vector3& point, const Sphere& sphere)
    {
        const Vector3 toPoint = point - sphere.center;
        const float length = Length(toPoint);
        if (length < 1e-6f) {
            // 中心と一致 → 方向が決まらないので任意の軸を採用する
            return sphere.center + Vector3{ sphere.radius, 0.0f, 0.0f };
        }
        return sphere.center + toPoint * (sphere.radius / length);
    }

    void ClosestPointsBetweenSegments(const LineSegment& a, const LineSegment& b,
                                      Vector3& outPointA, Vector3& outPointB)
    {
        const Vector3 dirA = a.end - a.start;
        const Vector3 dirB = b.end - b.start;
        const Vector3 startDelta = a.start - b.start;

        const float lenSqA = Dot(dirA, dirA);
        const float lenSqB = Dot(dirB, dirB);
        const float f = Dot(dirB, startDelta);

        float s = 0.0f;   // A 上のパラメータ
        float t = 0.0f;   // B 上のパラメータ

        if (lenSqA <= kDegenerateLengthSq && lenSqB <= kDegenerateLengthSq) {
            // 両方とも点
        }
        else if (lenSqA <= kDegenerateLengthSq) {
            t = MathCore::Clamp(f / lenSqB, 0.0f, 1.0f);
        }
        else {
            const float c = Dot(dirA, startDelta);

            if (lenSqB <= kDegenerateLengthSq) {
                s = MathCore::Clamp(-c / lenSqA, 0.0f, 1.0f);
            }
            else {
                const float dotAB = Dot(dirA, dirB);
                const float denom = lenSqA * lenSqB - dotAB * dotAB;

                // denom == 0 は 2 線分が平行。s は任意でよいので 0 に倒す。
                s = (denom != 0.0f)
                    ? MathCore::Clamp((dotAB * f - c * lenSqB) / denom, 0.0f, 1.0f)
                    : 0.0f;

                t = (dotAB * s + f) / lenSqB;

                // t が範囲外なら端にクランプし、その t に対する最適な s を取り直す
                if (t < 0.0f) {
                    t = 0.0f;
                    s = MathCore::Clamp(-c / lenSqA, 0.0f, 1.0f);
                }
                else if (t > 1.0f) {
                    t = 1.0f;
                    s = MathCore::Clamp((dotAB - c) / lenSqA, 0.0f, 1.0f);
                }
            }
        }

        outPointA = a.start + dirA * s;
        outPointB = b.start + dirB * t;
    }

    //================================================
    // 距離
    //================================================

    float DistancePointToPoint(const Vector3& a, const Vector3& b)
    {
        return Length(b - a);
    }

    float DistancePointToPlane(const Vector3& point, const Plane& plane)
    {
        return plane.DistanceTo(point);
    }

    float DistancePointToSegment(const Vector3& point, const LineSegment& segment)
    {
        return Length(point - ClosestPointOnSegment(point, segment));
    }

    float DistancePointToSphere(const Vector3& point, const Sphere& sphere)
    {
        return Length(point - sphere.center) - sphere.radius;
    }

    float DistancePointToAABB(const Vector3& point, const AABB& box)
    {
        return Length(point - ClosestPointOnAABB(point, box));
    }
}
}
