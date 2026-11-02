#include "pch.h"
#include "WindCVars.h"

#include <cmath>

namespace CoreEngine
{
    namespace WindCVars
    {
        CVar<float> Speed{ "r.Wind.Speed", 18.0f,
            "地上 10 m の風速 [m/s]（海の波の高さと白波の量、植物の揺れの強さが決まる。波は 32 m/s で飽和）",
            CVarRange{ 0.0f, 64.0f } };
        CVar<Vector2> Direction{ "r.Wind.Direction",
            Vector2{ 0.8411784172f, 0.5407575965f },
            "風が吹いていく向き（XZ。使うときに長さ 1 にする）", CVarRange{ -1.0f, 1.0f } };

        Vector2 NormalizedDirection()
        {
            const Vector2 direction = Direction.Get();
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            if (length < 1.0e-5f) {
                return Vector2{ 1.0f, 0.0f };
            }
            return Vector2{ direction.x / length, direction.y / length };
        }
    }
}
