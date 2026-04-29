#pragma once

#include "UIAnchor.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    struct UILayout
    {
        UIAnchor anchor      = UIAnchor::Center;
        Vector2  anchoredPos = { 0.0f, 0.0f };
        Vector2  pivot       = { 0.5f, 0.5f };
        Vector2  size        = { 100.0f, 100.0f };
        float    rotation    = 0.0f;
        int      sortOrder   = 0;

        Vector2 CalculateScreenPosition(const Vector2& canvasSize) const
        {
            Vector2 ap = GetAnchorPoint(anchor, canvasSize);
            return { ap.x + anchoredPos.x, ap.y + anchoredPos.y };
        }
    };

} // namespace CoreEngine
