#include "pch.h"
#include "Math/Geometry/RayCast.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace CoreEngine
{
namespace Geometry
{
    namespace {
        /// @brief レイが軸／平面と平行とみなす閾値
        constexpr float kParallelEpsilon = 1e-8f;

        /// @brief tMin 以降で最初に当たる t を選ぶ
        /// @param tEnter 入口 / @param tExit 出口
        /// @return 採用する t。範囲外なら false
        bool SelectHitParameter(float tEnter, float tExit, float tMin, float tMax, float& outT)
        {
            const float t = (tEnter >= tMin) ? tEnter : tExit;   // 原点が内部なら出口を返す
            if (t < tMin || t > tMax) {
                return false;
            }
            outT = t;
            return true;
        }
    }

    //================================================
    // レイ × 球
    //================================================

    bool Raycast(const Ray& ray, const Sphere& sphere, RayHit* outHit, float tMin, float tMax)
    {
        const Vector3 toOrigin = ray.origin - sphere.center;

        // direction は非正規化でもよい（t は direction 長を単位とする）
        const float a = Dot(ray.direction, ray.direction);
        if (a < kParallelEpsilon) {
            return false;   // 方向ベクトルがゼロ = レイとして成立しない
        }

        const float b = 2.0f * Dot(toOrigin, ray.direction);
        const float c = Dot(toOrigin, toOrigin) - sphere.radius * sphere.radius;

        const float discriminant = b * b - 4.0f * a * c;
        if (discriminant < 0.0f) {
            return false;
        }

        const float sqrtDiscriminant = std::sqrt(discriminant);
        const float inv2a = 1.0f / (2.0f * a);
        const float tEnter = (-b - sqrtDiscriminant) * inv2a;
        const float tExit  = (-b + sqrtDiscriminant) * inv2a;

        float t = 0.0f;
        if (!SelectHitParameter(tEnter, tExit, tMin, tMax, t)) {
            return false;
        }
        if (!outHit) {
            return true;
        }

        outHit->distance = t;
        outHit->point    = ray.origin + ray.direction * t;
        outHit->normal   = Normalize(outHit->point - sphere.center);
        return true;
    }

    //================================================
    // レイ × AABB（スラブ法）
    //================================================

    bool Raycast(const Ray& ray, const AABB& box, RayHit* outHit, float tMin, float tMax)
    {
        // 軸ごとに平行判定して 1/0 = inf を作らない。
        // 無条件に逆数を取ると、原点がスラブ境界にちょうど乗ったとき 0 * inf = NaN となり、
        // 棄却判定（tEnter > tExit）がすり抜ける。
        const float origin[3]    = { ray.origin.x,    ray.origin.y,    ray.origin.z };
        const float direction[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
        const float boxMin[3]    = { box.min.x,       box.min.y,       box.min.z };
        const float boxMax[3]    = { box.max.x,       box.max.y,       box.max.z };

        float tEnter = -(std::numeric_limits<float>::max)();
        float tExit  =  (std::numeric_limits<float>::max)();
        int   enterAxis = -1;
        float enterSign = 1.0f;
        bool  anyAxisConstrained = false;

        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(direction[axis]) < kParallelEpsilon) {
                // この軸に平行。スラブの外側にいるなら交差しない。
                if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) {
                    return false;
                }
                continue;   // 内側なら t を制約しない
            }

            const float inv = 1.0f / direction[axis];
            float tNear = (boxMin[axis] - origin[axis]) * inv;
            float tFar  = (boxMax[axis] - origin[axis]) * inv;

            float sign = -1.0f;   // tNear 側の面の外向き法線の符号
            if (tNear > tFar) {
                std::swap(tNear, tFar);
                sign = 1.0f;
            }

            if (tNear > tEnter) {
                tEnter = tNear;
                enterAxis = axis;
                enterSign = sign;
            }
            if (tFar < tExit) {
                tExit = tFar;
            }
            anyAxisConstrained = true;

            if (tEnter > tExit) {
                return false;
            }
        }

        if (!anyAxisConstrained) {
            return false;   // 方向ベクトルがゼロ
        }

        float t = 0.0f;
        if (!SelectHitParameter(tEnter, tExit, tMin, tMax, t)) {
            return false;
        }
        if (!outHit) {
            return true;
        }

        outHit->distance = t;
        outHit->point    = ray.origin + ray.direction * t;

        outHit->normal = Vector3{ 0.0f, 0.0f, 0.0f };
        if (enterAxis == 0)      outHit->normal.x = enterSign;
        else if (enterAxis == 1) outHit->normal.y = enterSign;
        else if (enterAxis == 2) outHit->normal.z = enterSign;
        return true;
    }

    //================================================
    // レイ × 平面
    //================================================

    bool Raycast(const Ray& ray, const Plane& plane, RayHit* outHit, float tMin, float tMax)
    {
        const float denom = Dot(plane.normal, ray.direction);
        if (std::abs(denom) < kParallelEpsilon) {
            return false;   // レイが平面と平行
        }

        // NOTE: 直前の epsilon チェックにより denom が 0 になることはないが、
        // Release 構成（/GL + 最適化有効）のみ MSVC のリンク時コード生成が denom を
        // コンパイル時定数 0 と誤認し C4723 を出す（Debug/Development では発生しない）。
        // volatile を経由させ、コンパイラによる定数畳み込みを避けて誤検知を防ぐ。
        volatile float safeDenom = denom;
        const float t = -plane.DistanceTo(ray.origin) / safeDenom;
        if (t < tMin || t > tMax) {
            return false;
        }
        if (!outHit) {
            return true;
        }

        outHit->distance = t;
        outHit->point    = ray.origin + ray.direction * t;
        // レイ側を向く法線を返す
        outHit->normal   = (denom < 0.0f) ? plane.normal : plane.normal * -1.0f;
        return true;
    }

    //================================================
    // レイ × 三角形（Möller–Trumbore）
    //================================================

    bool RaycastTriangle(const Ray& ray,
                         const Vector3& v0, const Vector3& v1, const Vector3& v2,
                         RayHit* outHit, float tMin, float tMax)
    {
        const Vector3 edge1 = v1 - v0;
        const Vector3 edge2 = v2 - v0;

        const Vector3 h = Cross(ray.direction, edge2);
        const float a = Dot(edge1, h);
        if (std::abs(a) < kParallelEpsilon) {
            return false;   // レイが三角形の平面と平行
        }

        const float f = 1.0f / a;
        const Vector3 s = ray.origin - v0;

        const float u = f * Dot(s, h);
        if (u < 0.0f || u > 1.0f) {
            return false;
        }

        const Vector3 q = Cross(s, edge1);
        const float v = f * Dot(ray.direction, q);
        if (v < 0.0f || u + v > 1.0f) {
            return false;
        }

        const float t = f * Dot(edge2, q);
        if (t < tMin || t > tMax) {
            return false;
        }
        if (!outHit) {
            return true;
        }

        outHit->distance = t;
        outHit->point    = ray.origin + ray.direction * t;

        // レイ側を向く法線を返す
        const Vector3 faceNormal = Normalize(Cross(edge1, edge2));
        outHit->normal = (Dot(faceNormal, ray.direction) < 0.0f) ? faceNormal : faceNormal * -1.0f;
        return true;
    }
}
}
