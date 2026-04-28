#pragma once

#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    /// @brief UI 要素のアンカー位置（Canvas 上の基準点）
    /// @details
    ///  Canvas の座標系: 左上 (0,0) → 右下 (canvasW, canvasH)
    ///  各アンカーは Canvas サイズに対する基準点を表す
    enum class UIAnchor
    {
        TopLeft,      // (0, 0)
        TopCenter,    // (w/2, 0)
        TopRight,     // (w, 0)
        MiddleLeft,   // (0, h/2)
        Center,       // (w/2, h/2)
        MiddleRight,  // (w, h/2)
        BottomLeft,   // (0, h)
        BottomCenter, // (w/2, h)
        BottomRight,  // (w, h)
    };

    /// @brief アンカー位置を Canvas サイズから実座標に変換
    /// @param anchor     アンカー種別
    /// @param canvasSize キャンバスサイズ（ピクセル）
    /// @return アンカー基準点の Canvas 座標
    inline Vector2 GetAnchorPoint(UIAnchor anchor, const Vector2& canvasSize)
    {
        switch (anchor) {
        case UIAnchor::TopLeft:      return { 0.0f, 0.0f };
        case UIAnchor::TopCenter:    return { canvasSize.x * 0.5f, 0.0f };
        case UIAnchor::TopRight:     return { canvasSize.x, 0.0f };
        case UIAnchor::MiddleLeft:   return { 0.0f, canvasSize.y * 0.5f };
        case UIAnchor::Center:       return { canvasSize.x * 0.5f, canvasSize.y * 0.5f };
        case UIAnchor::MiddleRight:  return { canvasSize.x,     canvasSize.y * 0.5f };
        case UIAnchor::BottomLeft:   return { 0.0f, canvasSize.y };
        case UIAnchor::BottomCenter: return { canvasSize.x * 0.5f, canvasSize.y };
        case UIAnchor::BottomRight:  return { canvasSize.x,     canvasSize.y };
        }
        return { 0.0f, 0.0f };
    }

} // namespace CoreEngine
