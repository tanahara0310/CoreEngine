#pragma once

#include "App/ViewState.h"

/// @file FrameStripView.h
/// @brief フレームの帯（記録の全体の細い帯と、表示中の 300 フレームの棒）

namespace CoreEngine::Profiler
{
    class FrameStripView
    {
    public:
        /// @brief 帯を描き、クリック・ドラッグ・ホイールで選んだフレームと帯の位置を view へ書く
        /// @param live 受け取りの最中なら true（「最新へ」のボタンを出す）
        void Draw(const FrameSnapshot& frames, ViewState& view, bool live);

    private:
        void DrawHeader(const FrameSnapshot& frames, ViewState& view, bool live);
        void DrawOverview(const FrameSnapshot& frames, ViewState& view, float labelWidth);
        void DrawBars(const FrameSnapshot& frames, ViewState& view, float labelWidth);
    };
}
