#pragma once

#include "UIAnchor.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

#include <cmath>

namespace CoreEngine
{
    /// @brief 回転を考えない矩形（キャンバス座標。左上が原点・Y 下正）
    struct UIRect
    {
        Vector2 min{ 0.0f, 0.0f };
        Vector2 max{ 0.0f, 0.0f };

        Vector2 Size() const { return { max.x - min.x, max.y - min.y }; }

        bool Contains(const Vector2& point) const
        {
            return point.x >= min.x && point.x <= max.x
                && point.y >= min.y && point.y <= max.y;
        }
    };

    /// @brief 回転を反映した 4 隅（キャンバス座標。右回りに並ぶ）
    struct UIQuad
    {
        Vector2 topLeft{};
        Vector2 topRight{};
        Vector2 bottomRight{};
        Vector2 bottomLeft{};

        /// @brief 点が中にあるか
        /// @details 4 辺を右回りに見て、どの辺から見ても左側に無いかを調べる。
        ///          回転していても効く。
        bool Contains(const Vector2& point) const
        {
            const Vector2 corners[4] = { topLeft, topRight, bottomRight, bottomLeft };
            for (int i = 0; i < 4; ++i) {
                const Vector2& a = corners[i];
                const Vector2& b = corners[(i + 1) % 4];
                const float edgeX = b.x - a.x;
                const float edgeY = b.y - a.y;
                const float toPointX = point.x - a.x;
                const float toPointY = point.y - a.y;
                if (edgeX * toPointY - edgeY * toPointX < 0.0f) {
                    return false;
                }
            }
            return true;
        }
    };

    /// @brief UI 要素の配置（アンカー・ピボット・サイズ・回転）
    struct UILayout
    {
        UIAnchor anchor      = UIAnchor::Center;
        Vector2  anchoredPos = { 0.0f, 0.0f };
        Vector2  pivot       = { 0.5f, 0.5f };
        Vector2  size        = { 100.0f, 100.0f };
        float    rotation    = 0.0f;
        int      sortOrder   = 0;

        /// @brief アンカーからのずらしを足した基準点（キャンバス座標）
        Vector2 CalculateScreenPosition(const Vector2& canvasSize) const
        {
            Vector2 ap = GetAnchorPoint(anchor, canvasSize);
            return { ap.x + anchoredPos.x, ap.y + anchoredPos.y };
        }

        /// @brief 基準点と大きさを反映した矩形（回転は見ない）
        /// @note 描画・当たり判定・エディタの枠は、必ずこれか `CalculateQuad` を通すこと。
        ///       別々に計算すると、見た目と当たり判定がずれる。
        UIRect CalculateRect(const Vector2& canvasSize) const
        {
            const Vector2 center = CalculateScreenPosition(canvasSize);
            UIRect rect;
            rect.min = { center.x - pivot.x * size.x,
                         center.y - pivot.y * size.y };
            rect.max = { center.x + (1.0f - pivot.x) * size.x,
                         center.y + (1.0f - pivot.y) * size.y };
            return rect;
        }

        /// @brief 回転を反映した 4 隅（キャンバス座標）
        UIQuad CalculateQuad(const Vector2& canvasSize) const
        {
            const Vector2 center = CalculateScreenPosition(canvasSize);
            const float left   = -pivot.x * size.x;
            const float right  = (1.0f - pivot.x) * size.x;
            const float top    = -pivot.y * size.y;
            const float bottom = (1.0f - pivot.y) * size.y;

            const float cosR = std::cos(rotation);
            const float sinR = std::sin(rotation);
            const auto corner = [&](float offsetX, float offsetY) -> Vector2 {
                return { center.x + offsetX * cosR - offsetY * sinR,
                         center.y + offsetX * sinR + offsetY * cosR };
                };

            UIQuad quad;
            quad.topLeft     = corner(left,  top);
            quad.topRight    = corner(right, top);
            quad.bottomRight = corner(right, bottom);
            quad.bottomLeft  = corner(left,  bottom);
            return quad;
        }

        /// @brief 点が要素の上にあるか（キャンバス座標）
        bool ContainsPoint(const Vector2& point, const Vector2& canvasSize) const
        {
            // 回っていないなら 4 辺の外積を回さずに済ませる
            if (rotation == 0.0f) {
                return CalculateRect(canvasSize).Contains(point);
            }
            return CalculateQuad(canvasSize).Contains(point);
        }
    };

} // namespace CoreEngine
