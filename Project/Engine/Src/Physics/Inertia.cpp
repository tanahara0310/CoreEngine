#include "pch.h"
#include "Inertia.h"

#include <numbers>

namespace CoreEngine
{
namespace Inertia
{
    Vector3 ForSphere(float mass, float radius)
    {
        // I = (2/5) m r²
        const float value = 0.4f * mass * radius * radius;
        return Vector3{ value, value, value };
    }

    Vector3 ForBox(float mass, const Vector3& size)
    {
        // Ixx = (1/12) m (h² + d²) ほか、軸ごとに残りの 2 辺を使う
        const float factor = mass / 12.0f;
        const float width2 = size.x * size.x;
        const float height2 = size.y * size.y;
        const float depth2 = size.z * size.z;

        return Vector3{
            factor * (height2 + depth2),
            factor * (width2 + depth2),
            factor * (width2 + height2),
        };
    }

    Vector3 ForCapsule(float mass, float radius, float cylinderLength)
    {
        const float r2 = radius * radius;
        const float h  = (cylinderLength > 0.0f) ? cylinderLength : 0.0f;

        // 円筒と両端の半球へ、体積の比で質量を配る
        const float pi = std::numbers::pi_v<float>;
        const float cylinderVolume = pi * r2 * h;
        const float sphereVolume   = (4.0f / 3.0f) * pi * r2 * radius;
        const float totalVolume    = cylinderVolume + sphereVolume;

        const float cylinderMass = (totalVolume > 0.0f) ? mass * (cylinderVolume / totalVolume) : 0.0f;
        const float sphereMass   = mass - cylinderMass;

        // 軸方向は円筒と球をそのまま足す
        const float axial = cylinderMass * r2 * 0.5f + sphereMass * r2 * 0.4f;

        // 横方向は半球のぶんを軸からの距離で押し出す
        const float radial =
            cylinderMass * (h * h / 12.0f + r2 * 0.25f)
            + sphereMass * (0.4f * r2 + 0.375f * radius * h + h * h * 0.25f);

        return Vector3{ radial, axial, radial };
    }

    Vector3 Invert(const Vector3& inertia)
    {
        return Vector3{
            (inertia.x > 0.0f) ? (1.0f / inertia.x) : 0.0f,
            (inertia.y > 0.0f) ? (1.0f / inertia.y) : 0.0f,
            (inertia.z > 0.0f) ? (1.0f / inertia.z) : 0.0f,
        };
    }
}
}
