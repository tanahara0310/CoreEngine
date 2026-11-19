#pragma once

#include "UIAnchor.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

#include <algorithm>
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

        /// @brief 2 つの矩形の重なり（重ならなければ大きさ 0 の矩形）
        UIRect Intersect(const UIRect& other) const
        {
            UIRect result;
            result.min = { (std::max)(min.x, other.min.x), (std::max)(min.y, other.min.y) };
            result.max = { (std::min)(max.x, other.max.x), (std::min)(max.y, other.max.y) };
            result.max = { (std::max)(result.max.x, result.min.x), (std::max)(result.max.y, result.min.y) };
            return result;
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

        /// @brief 4 隅を囲む矩形
        UIRect Bounds() const
        {
            UIRect rect;
            rect.min.x = (std::min)((std::min)(topLeft.x, topRight.x), (std::min)(bottomRight.x, bottomLeft.x));
            rect.min.y = (std::min)((std::min)(topLeft.y, topRight.y), (std::min)(bottomRight.y, bottomLeft.y));
            rect.max.x = (std::max)((std::max)(topLeft.x, topRight.x), (std::max)(bottomRight.x, bottomLeft.x));
            rect.max.y = (std::max)((std::max)(topLeft.y, topRight.y), (std::max)(bottomRight.y, bottomLeft.y));
            return rect;
        }
    };

    /// @brief UI 要素の配置（アンカー・位置・基準点・大きさ・回転・拡大・描画順）
    /// @details 位置と大きさは親の四角から測る。親の無い要素の親はキャンバス全体。
    ///          アンカーは親の四角の中の割合で、2 点が重なっていれば 1 点に付く。
    ///          2 点が離れていれば、その間の幅（高さ）に合わせて伸び、大きさはその幅との差になる。
    struct UILayout
    {
        Vector2  anchorMin   = { 0.5f, 0.5f };
        Vector2  anchorMax   = { 0.5f, 0.5f };
        Vector2  anchoredPos = { 0.0f, 0.0f };
        Vector2  pivot       = { 0.5f, 0.5f };
        Vector2  size        = { 100.0f, 100.0f };
        float    rotation    = 0.0f;
        Vector2  scale       = { 1.0f, 1.0f };
        int      sortOrder   = 0;

        /// @brief キャンバス全体の矩形（親の無い要素の親の四角）
        static UIRect CanvasRect(const Vector2& canvasSize) { return { { 0.0f, 0.0f }, canvasSize }; }

        /// @brief 横にアンカーが開いているか（親の幅に合わせて伸びるか）
        bool IsStretchedX() const { return anchorMin.x != anchorMax.x; }

        /// @brief 縦にアンカーが開いているか
        bool IsStretchedY() const { return anchorMin.y != anchorMax.y; }

        /// @brief 親の四角の中での大きさ（拡大は掛けない）
        Vector2 CalculateSize(const UIRect& parentRect) const
        {
            Vector2 result = size;
            if (IsStretchedX()) {
                result.x = (parentRect.max.x - parentRect.min.x) * (anchorMax.x - anchorMin.x) + size.x;
            }
            if (IsStretchedY()) {
                result.y = (parentRect.max.y - parentRect.min.y) * (anchorMax.y - anchorMin.y) + size.y;
            }
            return result;
        }

        /// @brief 親の四角の中での基準点の位置
        Vector2 CalculatePivotPosition(const UIRect& parentRect) const
        {
            const Vector2 parentSize = parentRect.Size();
            const auto axis = [](float parentMin, float parentLength, float lo, float hi, float pivotRatio) {
                const float from = parentMin + parentLength * lo;
                if (lo == hi) {
                    return from;
                }
                const float to = parentMin + parentLength * hi;
                return from + (to - from) * pivotRatio;
            };
            return {
                axis(parentRect.min.x, parentSize.x, anchorMin.x, anchorMax.x, pivot.x) + anchoredPos.x,
                axis(parentRect.min.y, parentSize.y, anchorMin.y, anchorMax.y, pivot.y) + anchoredPos.y,
            };
        }

        /// @brief 親の四角の中での、基準点と大きさと拡大を反映した矩形（回転は見ない）
        UIRect CalculateRect(const UIRect& parentRect) const
        {
            const Vector2 center = CalculatePivotPosition(parentRect);
            const Vector2 base = CalculateSize(parentRect);
            const Vector2 extent = { base.x * scale.x, base.y * scale.y };
            UIRect rect;
            rect.min = { center.x - pivot.x * extent.x,
                         center.y - pivot.y * extent.y };
            rect.max = { center.x + (1.0f - pivot.x) * extent.x,
                         center.y + (1.0f - pivot.y) * extent.y };
            return rect;
        }

        /// @brief 親の四角の中での、回転を反映した 4 隅
        UIQuad CalculateQuad(const UIRect& parentRect) const
        {
            const Vector2 center = CalculatePivotPosition(parentRect);
            const Vector2 base = CalculateSize(parentRect);
            const Vector2 extent = { base.x * scale.x, base.y * scale.y };
            const float left   = -pivot.x * extent.x;
            const float right  = (1.0f - pivot.x) * extent.x;
            const float top    = -pivot.y * extent.y;
            const float bottom = (1.0f - pivot.y) * extent.y;

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

        /// @brief 点が要素の上にあるか（親の四角と同じ座標）
        bool ContainsPoint(const Vector2& point, const UIRect& parentRect) const
        {
            // 回っていないなら 4 辺の外積を回さずに済ませる
            if (rotation == 0.0f) {
                return CalculateRect(parentRect).Contains(point);
            }
            return CalculateQuad(parentRect).Contains(point);
        }

        // ===== 親の無い要素（親の四角 = キャンバス全体） =====

        /// @brief アンカーからのずらしを足した基準点（キャンバス座標）
        Vector2 CalculateScreenPosition(const Vector2& canvasSize) const
        {
            return CalculatePivotPosition(CanvasRect(canvasSize));
        }

        /// @brief 基準点と大きさを反映した矩形（回転は見ない）
        /// @note 描画・当たり判定・エディタの枠は `RectTransformComponent` の形の問い合わせを使う
        UIRect CalculateRect(const Vector2& canvasSize) const
        {
            return CalculateRect(CanvasRect(canvasSize));
        }

        /// @brief 回転を反映した 4 隅（キャンバス座標）
        UIQuad CalculateQuad(const Vector2& canvasSize) const
        {
            return CalculateQuad(CanvasRect(canvasSize));
        }

        /// @brief 点が要素の上にあるか（キャンバス座標）
        bool ContainsPoint(const Vector2& point, const Vector2& canvasSize) const
        {
            return ContainsPoint(point, CanvasRect(canvasSize));
        }
    };

} // namespace CoreEngine
