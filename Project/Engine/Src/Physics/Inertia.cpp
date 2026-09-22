#include "pch.h"
#include "Inertia.h"

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
