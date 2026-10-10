#include "pch.h"
#include "Math/Curve/KeyCurve.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace CoreEngine::KeyCurve
{
    float Evaluate(const Vector2* keys, std::size_t count, float x)
    {
        if (!keys || count == 0) {
            return 0.0f;
        }
        std::vector<Vector2> points(keys, keys + count);
        std::stable_sort(points.begin(), points.end(), [](const Vector2& a, const Vector2& b) { return a.x < b.x; });
        if (count == 1 || x <= points.front().x) {
            return points.front().y;
        }
        if (x >= points.back().x) {
            return points.back().y;
        }

        // 区間ごとの傾き
        const std::size_t n = points.size();
        std::vector<float> slopes(n - 1);
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const float dx = points[i + 1].x - points[i].x;
            slopes[i] = dx > 0.0f ? (points[i + 1].y - points[i].y) / dx : 0.0f;
        }

        // 点ごとの接線（向きが変わる点は平らにし、行き過ぎないように縮める）
        std::vector<float> tangents(n);
        tangents.front() = slopes.front();
        tangents.back() = slopes.back();
        for (std::size_t i = 1; i + 1 < n; ++i) {
            tangents[i] = (slopes[i - 1] * slopes[i] <= 0.0f) ? 0.0f : (slopes[i - 1] + slopes[i]) * 0.5f;
        }
        for (std::size_t i = 0; i + 1 < n; ++i) {
            if (slopes[i] == 0.0f) {
                tangents[i] = 0.0f;
                tangents[i + 1] = 0.0f;
                continue;
            }
            const float a = tangents[i] / slopes[i];
            const float b = tangents[i + 1] / slopes[i];
            const float length = a * a + b * b;
            if (length > 9.0f) {
                const float scale = 3.0f / std::sqrt(length);
                tangents[i] = scale * a * slopes[i];
                tangents[i + 1] = scale * b * slopes[i];
            }
        }

        const auto upper = std::upper_bound(points.begin(), points.end(), x, [](float value, const Vector2& point) { return value < point.x; });
        const std::size_t i = static_cast<std::size_t>(std::distance(points.begin(), upper)) - 1;
        const float h = points[i + 1].x - points[i].x;
        if (h <= 0.0f) {
            return points[i + 1].y;
        }
        const float t = (x - points[i].x) / h;
        const float t2 = t * t;
        const float t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * points[i].y + (t3 - 2.0f * t2 + t) * h * tangents[i]
            + (-2.0f * t3 + 3.0f * t2) * points[i + 1].y + (t3 - t2) * h * tangents[i + 1];
    }
}
