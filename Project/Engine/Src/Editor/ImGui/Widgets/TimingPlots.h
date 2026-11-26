#pragma once

#include "Editor/ImGui/Widgets/PassTimingTable.h"
#include "Utility/Container/RingHistory.h"

#include <array>

/// @file
/// @brief フレームの時間と GPU の時間の履歴と、その線・積み上げのグラフ（ImPlot）

namespace CoreEngine::UI
{
    /// @brief 直近のフレームの時間と GPU の時間の履歴
    /// @details GPU の時間はカテゴリを 8 つのまとまりへ寄せて持つ。
    ///          雲の内訳と FFT 内訳（ほかのパスの内側を分けて測った時間）は、まとまりへ足さない。
    class TimingHistory
    {
    public:
        /// @brief 持つフレームの数
        static constexpr int kFrames = 120;

        /// @brief GPU の時間を積み上げるまとまりの数
        static constexpr int kGroupCount = 8;

        /// @brief 1 フレーム分を足す
        /// @param frameMs そのフレームにかかった時間（ミリ秒）
        /// @param gpuSlots そのフレームの GPU の計測（計測していないときは nullptr）
        void Push(float frameMs, const TimingSlots* gpuSlots);

        /// @brief フレームの時間と GPU の合計を重ねた線のグラフ（目標のフレーム時間に横線を引く）
        /// @param targetFps 目標の FPS（0 以下なら横線を引かない）
        void DrawFrameTimePlot(const char* id, float targetFps, float height) const;

        /// @brief まとまりごとの GPU の時間を下から積み上げた面のグラフ（上に GPU の合計の線を重ねる）
        void DrawGpuStackPlot(const char* id, float height) const;

    private:
        RingHistory<float, kFrames> frameMs_;
        RingHistory<float, kFrames> gpuMs_;
        std::array<RingHistory<float, kFrames>, kGroupCount> groupMs_;
        bool hasGpu_ = false;
    };
}
