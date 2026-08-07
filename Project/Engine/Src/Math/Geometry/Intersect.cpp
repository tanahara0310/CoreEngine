#include "pch.h"
#include "Math/Geometry/Intersect.h"
#include "Math/Geometry/Distance.h"

#include <cmath>

namespace CoreEngine
{
namespace Geometry
{
    namespace {
        /// @brief 方向が決まらないほど近い距離
        constexpr float kCoincidentEpsilon = 1e-6f;

        /// @brief 2 つの球状（中心 + 半径）の接触情報を組み立てる
        /// @param centerA A の中心 / @param radiusA A の半径
        /// @param centerB B の中心 / @param radiusB B の半径
        /// @param fallbackNormal 中心が一致して向きが決まらないときに使う法線
        /// @return 接触していれば true（outContact は接触時のみ書き込む）
        bool BuildRadialContact(const Vector3& centerA, float radiusA,
                                const Vector3& centerB, float radiusB,
                                const Vector3& fallbackNormal,
                                Contact* outContact)
        {
            const Vector3 delta = centerB - centerA;
            const float distanceSq = Dot(delta, delta);
            const float radiusSum = radiusA + radiusB;

            if (distanceSq > radiusSum * radiusSum) {
                return false;
            }
            if (!outContact) {
                return true;
            }

            const float distance = std::sqrt(distanceSq);
            const Vector3 normal = (distance > kCoincidentEpsilon)
                ? delta * (1.0f / distance)
                : fallbackNormal;

            outContact->normal = normal;
            outContact->depth  = radiusSum - distance;
            // 両者の表面の中間を代表点とする
            outContact->point  = centerA + normal * (radiusA - outContact->depth * 0.5f);
            return true;
        }
    }

    //================================================
    // 1 軸の重なり
    //================================================

    bool OverlapOnAxis(float aMin, float aMax, float bMin, float bMax, AxisOverlap& out)
    {
        // a を負方向へ動かして離す量 / 正方向へ動かして離す量
        const float pushNegative = aMax - bMin;
        const float pushPositive = bMax - aMin;

        if (pushNegative < 0.0f || pushPositive < 0.0f) {
            return false;   // 完全に分離している
        }

        if (pushNegative < pushPositive) {
            out.depth = pushNegative;
            out.direction = -1.0f;
        }
        else {
            out.depth = pushPositive;
            out.direction = 1.0f;
        }
        return true;
    }

    //================================================
    // 点の内包判定
    //================================================

    bool Contains(const AABB& box, const Vector3& point)
    {
        return point.x >= box.min.x && point.x <= box.max.x
            && point.y >= box.min.y && point.y <= box.max.y
            && point.z >= box.min.z && point.z <= box.max.z;
    }

    bool Contains(const Sphere& sphere, const Vector3& point)
    {
        const Vector3 delta = point - sphere.center;
        return Dot(delta, delta) <= sphere.radius * sphere.radius;
    }

    bool Contains(const Capsule& capsule, const Vector3& point)
    {
        const float distance = DistancePointToSegment(point, LineSegment(capsule.start, capsule.end));
        return distance <= capsule.radius;
    }

    //================================================
    // 球 × 球
    //================================================

    bool Intersect(const Sphere& a, const Sphere& b, Contact* outContact)
    {
        return BuildRadialContact(a.center, a.radius, b.center, b.radius,
                                  Vector3{ 0.0f, 1.0f, 0.0f }, outContact);
    }

    //================================================
    // AABB × AABB
    //================================================

    bool Intersect(const AABB& a, const AABB& b, Contact* outContact)
    {
        if (a.min.x > b.max.x || a.max.x < b.min.x ||
            a.min.y > b.max.y || a.max.y < b.min.y ||
            a.min.z > b.max.z || a.max.z < b.min.z) {
            return false;
        }
        if (!outContact) {
            return true;
        }

        // 各軸の重なり量。最小の軸が押し出し方向になる（最小移動量で分離できる向き）。
        const float aMin[3] = { a.min.x, a.min.y, a.min.z };
        const float aMax[3] = { a.max.x, a.max.y, a.max.z };
        const float bMin[3] = { b.min.x, b.min.y, b.min.z };
        const float bMax[3] = { b.max.x, b.max.y, b.max.z };

        AxisOverlap overlap[3]{};
        for (int axis = 0; axis < 3; ++axis) {
            OverlapOnAxis(aMin[axis], aMax[axis], bMin[axis], bMax[axis], overlap[axis]);
        }

        int minAxis = 0;
        if (overlap[1].depth < overlap[minAxis].depth) minAxis = 1;
        if (overlap[2].depth < overlap[minAxis].depth) minAxis = 2;

        // OverlapOnAxis の direction は「a を離す向き」。normal は a から b へ向かうので反転する。
        Vector3 normal{ 0.0f, 0.0f, 0.0f };
        const float sign = -overlap[minAxis].direction;
        if (minAxis == 0)      normal.x = sign;
        else if (minAxis == 1) normal.y = sign;
        else                   normal.z = sign;

        outContact->normal = normal;
        outContact->depth  = overlap[minAxis].depth;
        outContact->point  = Vector3{
            (std::max)(a.min.x, b.min.x) + overlap[0].depth * 0.5f,
            (std::max)(a.min.y, b.min.y) + overlap[1].depth * 0.5f,
            (std::max)(a.min.z, b.min.z) + overlap[2].depth * 0.5f,
        };
        return true;
    }

    //================================================
    // 球 × AABB（実装はここ 1 箇所だけ）
    //================================================

    bool Intersect(const Sphere& sphere, const AABB& box, Contact* outContact)
    {
        const Vector3 closest = ClosestPointOnAABB(sphere.center, box);
        const Vector3 delta = sphere.center - closest;
        const float distanceSq = Dot(delta, delta);

        if (distanceSq > sphere.radius * sphere.radius) {
            return false;
        }
        if (!outContact) {
            return true;
        }

        const float distance = std::sqrt(distanceSq);

        if (distance > kCoincidentEpsilon) {
            // 中心はボックスの外側。最近接点への向きがそのまま押し出し方向。
            const Vector3 outward = delta * (1.0f / distance);   // ボックス → 球
            outContact->normal = outward * -1.0f;                // 球 → ボックス
            outContact->depth  = sphere.radius - distance;
            outContact->point  = closest;
            return true;
        }

        // 中心がボックスの内部。最も近い面へ押し出す。
        const Vector3 toMin = sphere.center - box.min;
        const Vector3 toMax = box.max - sphere.center;
        const float faceDistance[6] = { toMin.x, toMax.x, toMin.y, toMax.y, toMin.z, toMax.z };

        int face = 0;
        for (int i = 1; i < 6; ++i) {
            if (faceDistance[i] < faceDistance[face]) face = i;
        }

        // face: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z（球から見た脱出方向の逆向き）
        Vector3 escape{ 0.0f, 0.0f, 0.0f };   // 球 → 外（球を押し出す向き）
        switch (face) {
        case 0: escape.x = -1.0f; break;
        case 1: escape.x =  1.0f; break;
        case 2: escape.y = -1.0f; break;
        case 3: escape.y =  1.0f; break;
        case 4: escape.z = -1.0f; break;
        default: escape.z = 1.0f; break;
        }

        outContact->normal = escape * -1.0f;   // 球 → ボックス
        outContact->depth  = sphere.radius + faceDistance[face];
        outContact->point  = sphere.center + escape * faceDistance[face];
        return true;
    }

    bool Intersect(const AABB& box, const Sphere& sphere, Contact* outContact)
    {
        // 実装は Sphere×AABB に一本化。引数順が逆なので法線を反転する。
        const bool hit = Intersect(sphere, box, outContact);
        if (hit && outContact) {
            outContact->normal = outContact->normal * -1.0f;
        }
        return hit;
    }

    //================================================
    // カプセル × 球（実装はここ 1 箇所だけ）
    //================================================

    bool Intersect(const Capsule& capsule, const Sphere& sphere, Contact* outContact)
    {
        const Vector3 onSegment =
            ClosestPointOnSegment(sphere.center, LineSegment(capsule.start, capsule.end));

        return BuildRadialContact(onSegment, capsule.radius, sphere.center, sphere.radius,
                                  Vector3{ 0.0f, 1.0f, 0.0f }, outContact);
    }

    bool Intersect(const Sphere& sphere, const Capsule& capsule, Contact* outContact)
    {
        const bool hit = Intersect(capsule, sphere, outContact);
        if (hit && outContact) {
            outContact->normal = outContact->normal * -1.0f;
        }
        return hit;
    }

    //================================================
    // カプセル × カプセル
    //================================================

    bool Intersect(const Capsule& a, const Capsule& b, Contact* outContact)
    {
        Vector3 pointA{}, pointB{};
        ClosestPointsBetweenSegments(LineSegment(a.start, a.end),
                                     LineSegment(b.start, b.end),
                                     pointA, pointB);

        return BuildRadialContact(pointA, a.radius, pointB, b.radius,
                                  Vector3{ 0.0f, 1.0f, 0.0f }, outContact);
    }
}
}
