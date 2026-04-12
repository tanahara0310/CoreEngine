#pragma once

#ifdef USE_IMGUI

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace CoreEngine
{
    class ThreadPool;

    /// @brief スレッドプールの動作状態をリアルタイムで可視化するデバッグUIパネル
    class ThreadProfilerUI
    {
    public:
        static constexpr int kHistorySize = 180;

        /// @brief スレッドプールをプロファイラに登録
        void RegisterPool(const std::string& name, std::function<ThreadPool*()> getter);

        /// @brief ImGuiパネルを描画
        void Draw();

    private:
        struct PoolEntry
        {
            std::string name;
            std::function<ThreadPool*()> getter;

            std::array<float, kHistorySize> activeHistory  = {};
            std::array<float, kHistorySize> pendingHistory = {};
            int      historyOffset = 0;
            uint32_t peakActive    = 0;  ///< 累計ピーク実行中タスク数
        };

        void DrawPool(PoolEntry& entry);

        std::vector<PoolEntry> pools_;
    };

} // namespace CoreEngine

#endif // USE_IMGUI
