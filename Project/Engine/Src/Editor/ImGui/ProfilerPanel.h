#pragma once

#ifdef CORE_EDITOR

#include <array>
#include <string>

namespace CoreEngine
{
    class EngineSystem;
    class GpuTimestampProfiler;
    class ThreadProfilerUI;

    /// @brief CPU・GPU・スクリプト・スレッドの時間を 1 画面にまとめたプロファイラ
    /// @details 直近 180 フレームの時間を控え、グラフと内訳を出す。
    class ProfilerPanel
    {
    public:
        /// @param engine 計測値を引くエンジン
        /// @param gpuProfiler パス別の CPU / GPU 時間
        /// @param threadProfiler スレッドプールの表示（無ければ nullptr）
        void Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler, ThreadProfilerUI* threadProfiler);

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
        void DrawScriptColumn();

        /// @brief 控えた履歴を CSV へ書く
        /// @return 書けたら true（`outPath` に書いた先）
        bool ExportCsv(std::string& outPath) const;

        /// @brief 古い順に i 番目の控え
        const FrameSample& SampleAt(int i) const;

        EngineSystem* engine_ = nullptr;
        GpuTimestampProfiler* gpuProfiler_ = nullptr;
        ThreadProfilerUI* threadProfiler_ = nullptr;

        std::array<FrameSample, kHistorySize> history_{};
        int historyHead_ = 0;   // 次に書く位置
        int historyCount_ = 0;  // 控えた数

        bool firstDraw_ = true;
        bool recording_ = true;
        bool showCpu_ = true;
        bool showGpu_ = true;
        bool showScript_ = true;
        bool showMemory_ = false;

        std::string lastExportPath_;
    };
}

#endif // CORE_EDITOR
