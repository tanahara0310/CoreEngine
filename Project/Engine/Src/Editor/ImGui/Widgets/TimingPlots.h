#pragma once

#include "Editor/ImGui/Widgets/PassTimingTable.h"
#include "Utility/Container/RingHistory.h"

#include <imgui.h>

#include <array>
#include <span>

/// @file
/// @brief フレームの時間の線のグラフと、GPU の時間の履歴の積み上げのグラフ（ImPlot）

namespace CoreEngine::UI
{
    /// @brief 時間の線のグラフの 1 行
    struct TimeLine
    {
        const char* label = nullptr;   ///< 凡例と吹き出しの名前
        const float* values = nullptr; ///< 古い順の値（ミリ秒）
        ImVec4 color{};                ///< 線の色
        bool tooltipOnly = false;      ///< 線を引かず、吹き出しにだけ直前の線の内訳として字下げして出す
    };

    /// @brief 直近のフレームの時間の線を重ねたグラフ（目標のフレーム時間に横線を引く）
    /// @param lines 線と吹き出しの行（凡例と吹き出しはこの順）
    /// @param count 1 行あたりの値の数
    /// @param capacity 横軸に並べるフレームの数（値が少ないうちは右へ寄せる）
    /// @param targetFps 目標の FPS（0 以下なら横線を引かない）
    void DrawTimeLinesPlot(const char* id, std::span<const TimeLine> lines, int count, int capacity,
        float targetFps, float height);

    /// @brief 直近のフレームの GPU の時間の履歴
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
        /// @param gpuSlots そのフレームの GPU の計測（計測していないときは nullptr）
        void Push(const TimingSlots* gpuSlots);

        /// @brief まとまりごとの GPU の時間を下から積み上げた面のグラフ（上に GPU の合計の線を重ねる）
        void DrawGpuStackPlot(const char* id, float height) const;

    private:
        RingHistory<float, kFrames> gpuMs_;
        std::array<RingHistory<float, kFrames>, kGroupCount> groupMs_;
    };
}
