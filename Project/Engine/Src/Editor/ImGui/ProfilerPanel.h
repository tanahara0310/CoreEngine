#pragma once

#ifdef CORE_EDITOR

#include "Utility/Container/RingHistory.h"

#include <string>

namespace CoreEngine
{
    class EngineSystem;
    class GpuTimestampProfiler;

    /// @brief CPU・GPU・スクリプトの時間を 1 画面にまとめたプロファイラ
    /// @details 直近 180 フレームの時間を控え、グラフと大まかな内訳を出す。
    class ProfilerPanel
    {
    public:
        /// @param engine 計測値を引くエンジン
        /// @param gpuProfiler パス別の CPU / GPU 時間
        void Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler);

        /// @brief 1 フレーム分の値を控える（描画が終わった後に 1 回呼ぶ）
        void Collect();

        /// @brief パネルの中身を描く（Begin / End は呼び出し元）
        void Draw();

    private:
        /// @brief 1 フレーム分の控え
        struct FrameSample
        {
            float frameMs = 0.0f;   ///< フレーム時間
            float updateMs = 0.0f;  ///< CPU：ゲームの更新
            float scriptMs = 0.0f;  ///< CPU：スクリプトの Update / LateUpdate
            float renderMs = 0.0f;  ///< CPU：描画の記録
            float gpuMs = 0.0f;     ///< GPU：フレーム合計
        };

        static constexpr int kHistorySize = 180;

        void DrawHeader();
        void DrawGraph();
        void DrawFrameBreakdown();

        /// @brief 控えた履歴を CSV へ書く
        /// @return 書けたら true（`outPath` に書いた先）
        bool ExportCsv(std::string& outPath) const;

        EngineSystem* engine_ = nullptr;
        GpuTimestampProfiler* gpuProfiler_ = nullptr;

        RingHistory<FrameSample, kHistorySize> history_;

        bool recording_ = true;
        bool showCpu_ = true;
        bool showGpu_ = true;
        bool showScript_ = true;
        bool showMemory_ = false;

        std::string lastExportPath_;
    };
}

#endif // CORE_EDITOR
