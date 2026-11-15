#pragma once

#ifdef CORE_EDITOR

#include "Editor/Environment/SunMoonController.h"

#include <optional>

namespace CoreEngine
{
    /// @brief 太陽と月の位置を描き、ドラッグで動かす空の図
    /// @details 方角・高度の目盛り・太陽の 1 日の軌道・シーンビューのカメラが見ている範囲を重ねる。
    ///          左ドラッグで太陽（月の印を掴めば月）、右ドラッグで月を動かし、Ctrl を押すと 5° ごとに止まる。
    class SkyMapWidget
    {
    public:
        /// @brief 図の描き方
        enum class View
        {
            Dome,      ///< 真上から見た空（中心が天頂・円が地平線）
            Panorama,  ///< 地平線をぐるりと広げた空（横が方位・縦が高度）
        };

        /// @brief 図を描いて操作を受ける（項目は 1 つ。直後に controller.TrackItem を呼んである）
        void Draw(SunMoonController& controller, View view);

    private:
        std::optional<SkyBody> dragging_; ///< ドラッグしている天体
    };
}

#endif // CORE_EDITOR
