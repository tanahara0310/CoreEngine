#include "pch.h"
#include "UI/UINavigation.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    Vector2 UINavigation::ToVector(UINavigationDirection direction)
    {
        switch (direction) {
        case UINavigationDirection::Up:    return { 0.0f, -1.0f };
        case UINavigationDirection::Down:  return { 0.0f,  1.0f };
        case UINavigationDirection::Left:  return { -1.0f, 0.0f };
        case UINavigationDirection::Right: return { 1.0f,  0.0f };
        }
        return { 0.0f, 0.0f };
    }

    Vector2 UINavigation::Center(const UIRect& rect)
    {
        return { (rect.min.x + rect.max.x) * 0.5f,
                 (rect.min.y + rect.max.y) * 0.5f };
    }

    Vector2 UINavigation::EdgePoint(const UIRect& rect, const Vector2& direction)
    {
        const Vector2 center = Center(rect);
        // 長い方の成分が 1 になるよう伸ばしてから半分の大きさを掛けると、辺の上に乗る
        const float longest = std::max(std::fabs(direction.x), std::fabs(direction.y));
        if (longest <= 0.0f) {
            return center;
        }
        const Vector2 size = rect.Size();
        return { center.x + size.x * 0.5f * (direction.x / longest),
                 center.y + size.y * 0.5f * (direction.y / longest) };
    }

    float UINavigation::Score(const Vector2& from, const Vector2& direction, const Vector2& target)
    {
        const float towardX = target.x - from.x;
        const float towardY = target.y - from.y;

        const float along = direction.x * towardX + direction.y * towardY;
        if (along <= 0.0f) {
            // 向きの後ろ、または真横に並んでいる
            return 0.0f;
        }
        const float squaredDistance = towardX * towardX + towardY * towardY;
        if (squaredDistance <= 0.0f) {
            return 0.0f;
        }
        return along / squaredDistance;
    }
}
