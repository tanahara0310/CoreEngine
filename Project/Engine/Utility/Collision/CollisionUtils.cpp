#include "CollisionUtils.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace CoreEngine
{

using namespace CoreEngine::MathCore;
namespace CollisionUtils {

    //================================================
    // Plane コンストラクタ
    //================================================

    Plane::Plane(const Vector3& normal, const Vector3& point)
        : normal(Vector::Normalize(normal))
        , distance(Vector::Dot(this->normal, point))
    {
    }

    //================================================
    // 距離計算
    //================================================

    float DistancePointToPoint(const Vector3& p1, const Vector3& p2) {
        return Vector::Length(Vector::Subtract(p2, p1));
    }

    float DistancePointToPlane(const Vector3& point, const Plane& plane) {
        return Vector::Dot(plane.normal, point) - plane.distance;
    }

    float DistancePointToLineSegment(const Vector3& point, const LineSegment& segment) {
        Vector3 segmentVec = Vector::Subtract(segment.end, segment.start);
        Vector3 pointVec = Vector::Subtract(point, segment.start);
        
        float segmentLengthSq = Vector::Dot(segmentVec, segmentVec);
        if (segmentLengthSq < 1e-6f) {
            // 線分の長さが0の場合は点との距離
            return Vector::Length(pointVec);
        }
        
        float t = Vector::Dot(pointVec, segmentVec) / segmentLengthSq;
        t = Clamp(t, 0.0f, 1.0f);
        
        Vector3 closestPoint = Vector::Add(segment.start, Vector::Multiply(t, segmentVec));
        return Vector::Length(Vector::Subtract(point, closestPoint));
    }

    float DistancePointToSphere(const Vector3& point, const Sphere& sphere) {
        float distanceToCenter = Vector::Length(Vector::Subtract(point, sphere.center));
        return distanceToCenter - sphere.radius;
    }

    float DistancePointToAABB(const Vector3& point, const BoundingBox& aabb) {
        Vector3 closestPoint = ClosestPointOnAABB(point, aabb);
        Vector3 diff = Vector::Subtract(point, closestPoint);
        return Vector::Length(diff);
    }

    //================================================
    // 最近接点計算
    //================================================

    Vector3 ClosestPointOnLineSegment(const Vector3& point, const LineSegment& segment) {
        Vector3 segmentVec = Vector::Subtract(segment.end, segment.start);
        Vector3 pointVec = Vector::Subtract(point, segment.start);
        
        float segmentLengthSq = Vector::Dot(segmentVec, segmentVec);
        if (segmentLengthSq < 1e-6f) {
            return segment.start;
        }
        
        float t = Vector::Dot(pointVec, segmentVec) / segmentLengthSq;
        t = Clamp(t, 0.0f, 1.0f);
        
        return Vector::Add(segment.start, Vector::Multiply(t, segmentVec));
    }

    Vector3 ClosestPointOnPlane(const Vector3& point, const Plane& plane) {
        float distance = DistancePointToPlane(point, plane);
        return Vector::Subtract(point, Vector::Multiply(distance, plane.normal));
    }

    Vector3 ClosestPointOnAABB(const Vector3& point, const BoundingBox& aabb) {
        return Clamp(point, aabb.min, aabb.max);
    }

    Vector3 ClosestPointOnSphere(const Vector3& point, const Sphere& sphere) {
        Vector3 direction = Vector::Subtract(point, sphere.center);
        float distance = Vector::Length(direction);
        
        if (distance < 1e-6f) {
            // 点が球の中心にある場合は任意の方向
            return Vector::Add(sphere.center, {sphere.radius, 0.0f, 0.0f});
        }
        
        direction = Vector::Normalize(direction);
        return Vector::Add(sphere.center, Vector::Multiply(sphere.radius, direction));
    }

    //================================================
    // 当たり判定（基本形状）
    //================================================

    bool IsColliding(const Vector3& point, const BoundingBox& aabb) {
        return (point.x >= aabb.min.x && point.x <= aabb.max.x &&
                point.y >= aabb.min.y && point.y <= aabb.max.y &&
                point.z >= aabb.min.z && point.z <= aabb.max.z);
    }

    bool IsColliding(const Vector3& point, const Sphere& sphere) {
        float distanceSq = Vector::Dot(
            Vector::Subtract(point, sphere.center), 
            Vector::Subtract(point, sphere.center)
        );
        return distanceSq <= (sphere.radius * sphere.radius);
    }

    bool IsColliding(const Sphere& sphere1, const Sphere& sphere2) {
        float distanceSq = Vector::Dot(
            Vector::Subtract(sphere2.center, sphere1.center), 
            Vector::Subtract(sphere2.center, sphere1.center)
        );
        float radiusSum = sphere1.radius + sphere2.radius;
        return distanceSq <= (radiusSum * radiusSum);
    }

    bool IsColliding(const BoundingBox& aabb1, const BoundingBox& aabb2) {
        return (aabb1.min.x <= aabb2.max.x && aabb1.max.x >= aabb2.min.x &&
                aabb1.min.y <= aabb2.max.y && aabb1.max.y >= aabb2.min.y &&
                aabb1.min.z <= aabb2.max.z && aabb1.max.z >= aabb2.min.z);
    }

    bool IsColliding(const Sphere& sphere, const BoundingBox& aabb) {
        Vector3 closestPoint = ClosestPointOnAABB(sphere.center, aabb);
        float distanceSq = Vector::Dot(
            Vector::Subtract(sphere.center, closestPoint), 
            Vector::Subtract(sphere.center, closestPoint)
        );
        return distanceSq <= (sphere.radius * sphere.radius);
    }

    bool IsColliding(const Vector3& point, const Capsule& capsule) {
        LineSegment segment(capsule.start, capsule.end);
        float distance = DistancePointToLineSegment(point, segment);
        return distance <= capsule.radius;
    }

    bool IsColliding(const Capsule& capsule, const Sphere& sphere) {
        LineSegment segment(capsule.start, capsule.end);
        float distance = DistancePointToLineSegment(sphere.center, segment);
        return distance <= (capsule.radius + sphere.radius);
    }

    bool IsColliding(const Capsule& capsule1, const Capsule& capsule2) {
        // 二つの線分間の最短距離を計算し、半径の合計と比較
        LineSegment seg1(capsule1.start, capsule1.end);
        LineSegment seg2(capsule2.start, capsule2.end);
        
        Vector3 d1 = Vector::Subtract(seg1.end, seg1.start);
        Vector3 d2 = Vector::Subtract(seg2.end, seg2.start);
        Vector3 r = Vector::Subtract(seg1.start, seg2.start);
        
        float a = Vector::Dot(d1, d1);
        float e = Vector::Dot(d2, d2);
        float f = Vector::Dot(d2, r);
        
        float s, t;
        
        if (a <= 1e-6f && e <= 1e-6f) {
            // 両方が点の場合
            s = t = 0.0f;
        } else if (a <= 1e-6f) {
            // seg1が点の場合
            s = 0.0f;
            t = Clamp(f / e, 0.0f, 1.0f);
        } else {
            float c = Vector::Dot(d1, r);
            if (e <= 1e-6f) {
                // seg2が点の場合
                t = 0.0f;
                s = Clamp(-c / a, 0.0f, 1.0f);
            } else {
                // 一般的なケース
                float b = Vector::Dot(d1, d2);
                float denom = a * e - b * b;
                
                if (denom != 0.0f) {
                    s = Clamp((b * f - c * e) / denom, 0.0f, 1.0f);
                } else {
                    s = 0.0f;
                }
                
                t = (b * s + f) / e;
                
                if (t < 0.0f) {
                    t = 0.0f;
                    s = Clamp(-c / a, 0.0f, 1.0f);
                } else if (t > 1.0f) {
                    t = 1.0f;
                    s = Clamp((b - c) / a, 0.0f, 1.0f);
                }
            }
        }
        
        Vector3 c1 = Vector::Add(seg1.start, Vector::Multiply(s, d1));
        Vector3 c2 = Vector::Add(seg2.start, Vector::Multiply(t, d2));
        
        float distance = Vector::Length(Vector::Subtract(c1, c2));
        return distance <= (capsule1.radius + capsule2.radius);
    }

    //================================================
    // レイ当たり判定
    //================================================

    std::optional<Vector3> RayIntersectPlane(const Ray& ray, const Plane& plane) {
        float denom = Vector::Dot(plane.normal, ray.direction);
        
        if (std::abs(denom) < 1e-6f) {
            // レイが平面と平行
            return std::nullopt;
        }
        
        float t = (plane.distance - Vector::Dot(plane.normal, ray.origin)) / denom;
        
        if (t < 0.0f) {
            // 交点がレイの後方
            return std::nullopt;
        }
        
        return Vector::Add(ray.origin, Vector::Multiply(t, ray.direction));
    }

    std::optional<Vector3> RayIntersectSphere(const Ray& ray, const Sphere& sphere) {
        float distance;
        auto result = RayIntersectSphere(ray, sphere, distance);
        return result;
    }

    std::optional<Vector3> RayIntersectSphere(const Ray& ray, const Sphere& sphere, float& outDistance) {
        Vector3 oc = Vector::Subtract(ray.origin, sphere.center);
        
        float a = Vector::Dot(ray.direction, ray.direction);
        float b = 2.0f * Vector::Dot(oc, ray.direction);
        float c = Vector::Dot(oc, oc) - sphere.radius * sphere.radius;
        
        float discriminant = b * b - 4.0f * a * c;
        
        if (discriminant < 0.0f) {
            return std::nullopt;
        }
        
        float t1 = (-b - std::sqrt(discriminant)) / (2.0f * a);
        float t2 = (-b + std::sqrt(discriminant)) / (2.0f * a);
        
        float t = (t1 >= 0.0f) ? t1 : t2;
        
        if (t < 0.0f) {
            return std::nullopt;
        }
        
        outDistance = t;
        return Vector::Add(ray.origin, Vector::Multiply(t, ray.direction));
    }

    std::optional<Vector3> RayIntersectAABB(const Ray& ray, const BoundingBox& aabb) {
        Vector3 invDir = {
            1.0f / ray.direction.x,
            1.0f / ray.direction.y,
            1.0f / ray.direction.z
        };
        
        float t1 = (aabb.min.x - ray.origin.x) * invDir.x;
        float t2 = (aabb.max.x - ray.origin.x) * invDir.x;
        if (t1 > t2) std::swap(t1, t2);
        
        float t3 = (aabb.min.y - ray.origin.y) * invDir.y;
        float t4 = (aabb.max.y - ray.origin.y) * invDir.y;
        if (t3 > t4) std::swap(t3, t4);
        
        float t5 = (aabb.min.z - ray.origin.z) * invDir.z;
        float t6 = (aabb.max.z - ray.origin.z) * invDir.z;
        if (t5 > t6) std::swap(t5, t6);
        
        float tmin = std::max({t1, t3, t5});
        float tmax = std::min({t2, t4, t6});
        
        if (tmax < 0.0f || tmin > tmax) {
            return std::nullopt;
        }
        
        float t = (tmin >= 0.0f) ? tmin : tmax;
        if (t < 0.0f) {
            return std::nullopt;
        }
        
        return Vector::Add(ray.origin, Vector::Multiply(t, ray.direction));
    }

    //================================================
    // 便利関数
    //================================================

    float PointPlaneDistance(const Vector3& point, const Plane& plane) {
        return Vector::Dot(plane.normal, point) - plane.distance;
    }

    float Clamp(float value, float min, float max) {
        return (std::max)(min, (std::min)(max, value));
    }

    Vector3 Clamp(const Vector3& value, const Vector3& min, const Vector3& max) {
        return {
            Clamp(value.x, min.x, max.x),
            Clamp(value.y, min.y, max.y),
            Clamp(value.z, min.z, max.z)
        };
    }

    float Lerp(float start, float end, float t) {
        return start + t * (end - start);
    }

    Vector3 Lerp(const Vector3& start, const Vector3& end, float t) {
        return {
            Lerp(start.x, end.x, t),
            Lerp(start.y, end.y, t),
            Lerp(start.z, end.z, t)
        };
    }

    Vector3 Slerp(const Vector3& start, const Vector3& end, float t) {
        float dot = Vector::Dot(start, end);
        dot = Clamp(dot, -1.0f, 1.0f);
        
        float theta = std::acos(dot) * t;
        Vector3 relativeVec = Vector::Normalize(Vector::Subtract(end, Vector::Multiply(dot, start)));
        
        return Vector::Add(
            Vector::Multiply(std::cos(theta), start),
            Vector::Multiply(std::sin(theta), relativeVec)
        );
    }

    Vector3 ClampToAABB(const Vector3& vector, const BoundingBox& aabb) {
        return Clamp(vector, aabb.min, aabb.max);
    }

    BoundingBox ExpandAABB(const BoundingBox& aabb, float expansion) {
        Vector3 expand = {expansion, expansion, expansion};
        return {
            Vector::Subtract(aabb.min, expand),
            Vector::Add(aabb.max, expand)
        };
    }

    BoundingBox ExpandAABB(const BoundingBox& aabb, const Vector3& expansion) {
        return {
            Vector::Subtract(aabb.min, expansion),
            Vector::Add(aabb.max, expansion)
        };
    }

    BoundingBox ExpandAABBWithPoint(const BoundingBox& aabb, const Vector3& point) {
        return {
            {
                std::min(aabb.min.x, point.x),
                std::min(aabb.min.y, point.y),
                std::min(aabb.min.z, point.z)
            },
            {
                std::max(aabb.max.x, point.x),
                std::max(aabb.max.y, point.y),
                std::max(aabb.max.z, point.z)
            }
        };
    }

    BoundingBox CreateAABBFromPoints(const Vector3* points, size_t count) {
        if (count == 0) {
            return BoundingBox();
        }
        
        BoundingBox result(points[0], points[0]);
        
        for (size_t i = 1; i < count; ++i) {
            result = ExpandAABBWithPoint(result, points[i]);
        }
        
        return result;
    }

} // namespace CollisionUtils
}
