#include "pch.h"
#include "ThreadProfilerUI.h"

#ifdef USE_IMGUI

#include "Threading/ThreadPool.h"
#include "Threading/ThreadBudget.h"
#include "Editor/ImGui/ImGuiAll.h"

#include <algorithm>
#include <format>
#include <map>

namespace CoreEngine
{
    namespace
    {
        // ─── パレット ────────────────────────────────────────────────
        constexpr ImVec4 kColBusy = { 0.25f, 0.88f, 0.42f, 1.0f }; // 緑   : 実行中
        constexpr ImVec4 kColIdle = { 0.42f, 0.42f, 0.46f, 1.0f }; // 灰   : 待機中
        constexpr ImVec4 kColQueue = { 0.95f, 0.68f, 0.15f, 1.0f }; // 琥珀 : キュー待ち
        constexpr ImVec4 kColWarn = { 0.98f, 0.45f, 0.35f, 1.0f }; // 赤   : 警告
        constexpr ImVec4 kColDim = { 0.58f, 0.58f, 0.62f, 1.0f }; // 補足テキスト
        constexpr ImVec4 kColValue = { 0.92f, 0.92f, 0.94f, 1.0f };
        constexpr ImVec4 kColGraph = { 0.35f, 0.78f, 0.95f, 1.0f };
        constexpr ImVec4 kColFrameBg = { 0.10f, 0.10f, 0.12f, 1.0f };

        /// @brief 占有率に応じた色。低いほど赤寄りにして「遊んでいる」ことを目立たせる
        ImVec4 OccupancyColor(double occupancy)
        {
            if (occupancy >= 0.70) return kColBusy;
            if (occupancy >= 0.35) return kColQueue;
            return kColWarn;
        }

        /// @brief 「ラベル」「値」を横並びで 1 組出す（表を組まずに数字を並べるため）
        void Metric(const char* label, const std::string& value, const ImVec4& color)
        {
            ImGui::TextColored(kColDim, "%s", label);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(color, "%s", value.c_str());
        }

        /// @brief ラベルの ':' より前をタスク種別として切り出す
        std::string GroupKeyOf(const std::string& label)
        {
            const size_t pos = label.find(':');
            if (pos == std::string::npos) {
                return label.empty() ? std::string("(無名)") : label;
            }
            return label.substr(0, pos);
        }

        struct GroupAggregate
        {
            uint32_t count = 0;
            double   totalRunMs = 0.0;
            double   maxRunMs = 0.0;
            double   totalWaitMs = 0.0;
        };
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::RegisterPool(const std::string& name, std::function<ThreadPool*()> getter)
    {
        for (auto& e : pools_) {
            if (e.name == name) { e.getter = std::move(getter); return; }
        }
        PoolEntry e;
        e.name = name;
        e.getter = std::move(getter);
        pools_.push_back(std::move(e));
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::Draw()
    {
        DrawBudgetSummary();

        if (pools_.empty()) {
            ImGui::TextDisabled("スレッドプールが登録されていません");
            return;
        }

        ImGui::Spacing();

        for (auto& entry : pools_) {
            DrawPool(entry);
        }
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::DrawBudgetSummary()
    {
        const uint32_t capacity = ThreadBudget::GetCapacity();
        const uint32_t reserved = ThreadBudget::GetReserved();
        const bool over = reserved > capacity;

        ImGui::TextColored(kColDim, "ワーカー枠");
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(over ? kColWarn : kColValue, "%u / %u", reserved, capacity);
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextColored(kColDim, "(コア数 - 1。メインスレッドのぶんを 1 本残す)");

        if (over) {
            ImGui::TextColored(kColWarn,
                "  警告: 枠を超えて予約されています。コア数より多いスレッドが走ると"
                "スループットは上がらずレイテンシだけ悪化します");
        }
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::DrawPool(PoolEntry& entry)
    {
        ThreadPool* pool = entry.getter ? entry.getter() : nullptr;
        if (!pool) {
            ImGui::TextColored(kColDim, "[%s]  未生成（最初の非同期要求で作られます）",
                entry.name.c_str());
            return;
        }

        ImGui::PushID(entry.name.c_str());

        const auto stats = pool->GetStats();
        const uint32_t workerCount = stats.workerCount;
        const double occupancy = stats.GetOccupancy();
        const double speedup = stats.GetParallelSpeedup();

        entry.peakActive = (std::max)(entry.peakActive, stats.activeTasks);

        entry.activeHistory[entry.historyOffset] = static_cast<float>(stats.activeTasks);
        entry.pendingHistory[entry.historyOffset] = static_cast<float>(stats.pendingTasks);
        entry.historyOffset = (entry.historyOffset + 1) % kHistorySize;

        // ─── ヘッダ: 名前とこのプールの成績を 1 行で ───────────────
        // 「占有率」と「直列比」をヘッダに出すのが今回の要点。
        // 折り畳んだままでも並列化が効いているかが読める。
        const std::string header = std::format(
            "{}   {}ワーカー   占有率 {:.0f}%   直列比 {:.1f}x###hdr",
            entry.name, workerCount, occupancy * 100.0, speedup);

        const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        if (!open) {
            ImGui::PopID();
            return;
        }

        ImGui::Indent(8.0f);

        // ─── 主要な数字を 2 行に凝縮 ───────────────────────────────
        Metric("実行中", std::format("{}", stats.activeTasks),
            stats.activeTasks > 0 ? kColBusy : kColDim);
        ImGui::SameLine(0.0f, 16.0f);
        Metric("キュー待ち", std::format("{}", stats.pendingTasks),
            stats.pendingTasks > 0 ? kColQueue : kColDim);
        ImGui::SameLine(0.0f, 16.0f);
        Metric("完了", std::format("{} / {}", stats.totalCompleted, stats.totalSubmitted), kColValue);
        ImGui::SameLine(0.0f, 16.0f);
        Metric("同時実行ピーク", std::format("{}", stats.peakActiveTasks), kColValue);

        // 「稼働区間」は最初のタスク開始から最後のタスク終了までの幅。
        // 占有率と直列比はこれで割る（経過時間で割ると、起動時に働いて以降暇な
        // ロード系プールが必ず 0% になり「並列化が効いていない」と誤読する）
        Metric("稼働区間", std::format("{:.0f}ms", stats.activeSpanMs), kColValue);
        ImGui::SameLine(0.0f, 16.0f);
        Metric("ワーカー実行時間合計", std::format("{:.0f}ms", stats.totalBusyMs), kColValue);
        ImGui::SameLine(0.0f, 16.0f);
        Metric("最大待ち", std::format("{:.1f}ms", stats.maxWaitMs),
            stats.maxWaitMs > stats.maxRunMs ? kColWarn : kColDim);
        ImGui::SameLine(0.0f, 16.0f);
        Metric("最大実行", std::format("{:.1f}ms", stats.maxRunMs), kColValue);

        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::SmallButton("統計リセット")) {
            pool->ResetStats();
            entry.peakActive = 0;
            entry.activeHistory.fill(0.0f);
            entry.pendingHistory.fill(0.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "起動中の集計とゲーム中の集計は混ぜると読めなくなります。\n"
                "ゲーム中の挙動を見るときは一度リセットしてください");
        }

        // ─── 占有率バー ─────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, OccupancyColor(occupancy));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kColFrameBg);
        const std::string overlay = std::format(
            "占有率 {:.0f}%   ({:.0f}ms / 稼働区間 {:.0f}ms × {}ワーカー)   計測開始から {:.1f}s 経過",
            occupancy * 100.0, stats.totalBusyMs, stats.activeSpanMs, workerCount,
            stats.elapsedMs / 1000.0);
        ImGui::ProgressBar(static_cast<float>(occupancy), ImVec2(-1.0f, 18.0f), overlay.c_str());
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // ─── ワーカーごとの状態（何が走っているか）───────────────
        DrawWorkerTable(*pool);

        ImGui::Spacing();

        // ─── 実行中タスク数の推移（1 本だけ・薄く）─────────────────
        ImGui::PushStyleColor(ImGuiCol_PlotLines, kColGraph);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kColFrameBg);
        const float maxActive = static_cast<float>(workerCount > 0 ? workerCount : 1);
        ImGui::PlotLines("##active", entry.activeHistory.data(), kHistorySize,
            entry.historyOffset, "実行中タスク数", 0.0f, maxActive,
            ImVec2(-1.0f, 34.0f));
        ImGui::PopStyleColor(2);

        // ─── 集計表 ─────────────────────────────────────────────────
        ImGui::Spacing();
        if (ImGui::TreeNodeEx("##groups", entry.showGroups ? ImGuiTreeNodeFlags_DefaultOpen : 0,
            "タスク種別ごとの集計")) {
            DrawGroupTable(*pool);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("##tasks", entry.showTasks ? ImGuiTreeNodeFlags_DefaultOpen : 0,
            "直近タスク一覧（列見出しでソート）")) {
            DrawTaskTable(*pool);
            ImGui::TreePop();
        }

        ImGui::Unindent(8.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::DrawWorkerTable(const ThreadPool& pool)
    {
        // ワーカー 1 本ごとの稼働状況。占有率と直近タスク名で「遊んでいないか」を見る
        const auto stats = pool.GetStats();

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

        if (!ImGui::BeginTable("##workers", 5, flags)) {
            return;
        }

        ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        ImGui::TableSetupColumn("実行中のタスク", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("経過", ImGuiTableColumnFlags_WidthFixed, 68.0f);
        ImGui::TableSetupColumn("完了 / 稼働", ImGuiTableColumnFlags_WidthFixed, 118.0f);
        ImGui::TableHeadersRow();

        for (uint32_t i = 0; i < stats.workerCount && i < ThreadPool::kMaxWorkers; ++i) {
            const auto& worker = stats.workers[i];

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%u", i);

            ImGui::TableNextColumn();
            ImGui::TextColored(worker.busy ? kColBusy : kColIdle,
                worker.busy ? "実行中" : "待機");

            ImGui::TableNextColumn();
            if (worker.busy) {
                ImGui::TextColored(kColValue, "%s", worker.currentTask.c_str());
            } else {
                ImGui::TextColored(kColIdle, "-");
            }

            ImGui::TableNextColumn();
            if (worker.busy) {
                ImGui::TextColored(kColValue, "%.1fms", worker.currentElapsedMs);
            } else {
                ImGui::TextColored(kColIdle, "-");
            }

            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%llu / %.0fms",
                static_cast<unsigned long long>(worker.completedTasks), worker.busyMs);
        }

        ImGui::EndTable();
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::DrawGroupTable(const ThreadPool& pool)
    {
        // タスクラベルの接頭辞でグルーピングして「どの種類が重いか」を出す。
        // 個々のタスク行だけでは合計が見えず、粒度の判断ができない
        const auto records = pool.GetRecentTasks();
        if (records.empty()) {
            ImGui::TextColored(kColDim, "  記録されたタスクがありません");
            return;
        }

        std::map<std::string, GroupAggregate> groups;
        for (const auto& record : records) {
            GroupAggregate& aggregate = groups[GroupKeyOf(record.label)];
            aggregate.count += 1;
            aggregate.totalRunMs += record.runMs;
            aggregate.maxRunMs = (std::max)(aggregate.maxRunMs, record.runMs);
            aggregate.totalWaitMs += record.waitMs;
        }

        ImGui::TextColored(kColDim, "  直近 %zu 件から集計（それより古いタスクは含みません）",
            records.size());

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;

        if (!ImGui::BeginTable("##groupTable", 5, flags)) {
            return;
        }

        ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("件数", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("合計", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("平均", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("最大", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (const auto& [key, aggregate] : groups) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(kColValue, "%s", key.c_str());

            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%u", aggregate.count);

            ImGui::TableNextColumn();
            ImGui::TextColored(kColValue, "%.0fms", aggregate.totalRunMs);

            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%.1fms",
                aggregate.totalRunMs / static_cast<double>(aggregate.count));

            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%.1fms", aggregate.maxRunMs);
        }

        ImGui::EndTable();
    }

    // ─────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::DrawTaskTable(const ThreadPool& pool)
    {
        auto records = pool.GetRecentTasks();
        if (records.empty()) {
            ImGui::TextColored(kColDim, "  記録されたタスクがありません");
            return;
        }

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

        if (!ImGui::BeginTable("##taskTable", 5, flags, ImVec2(0.0f, 200.0f))) {
            return;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("タスク", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("待ち",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 72.0f);
        ImGui::TableSetupColumn("実行",
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 72.0f);
        ImGui::TableSetupColumn("開始", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        // 既定は「実行時間の降順」。長い棒を探すのがこの表の目的なので、
        // 何もしなくても最初から答えが上に来ているべき。
        int sortColumn = 3;
        bool ascending = false;
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
            if (specs->SpecsCount > 0) {
                sortColumn = specs->Specs[0].ColumnIndex;
                ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            }
        }

        std::stable_sort(records.begin(), records.end(),
            [sortColumn, ascending](const ThreadPool::TaskRecord& a,
                const ThreadPool::TaskRecord& b) {
                    bool less = false;
                    switch (sortColumn) {
                    case 0:  less = a.label < b.label; break;
                    case 1:  less = a.workerIndex < b.workerIndex; break;
                    case 2:  less = a.waitMs < b.waitMs; break;
                    case 4:  less = a.startMs < b.startMs; break;
                    default: less = a.runMs < b.runMs; break;
                    }
                    return ascending ? less : !less;
            });

        for (const auto& record : records) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(kColValue, "%s", record.label.c_str());

            ImGui::TableNextColumn();
            if (record.workerIndex >= ThreadPool::kMaxWorkers) {
                // プール外のスレッドが Wait() 経由で肩代わりしたぶん
                ImGui::TextColored(kColQueue, "手伝");
            } else {
                ImGui::TextColored(kColDim, "%u", record.workerIndex);
            }

            ImGui::TableNextColumn();
            // 待ちが実行より長いタスクは「並列度が足りていない」ことの直接の証拠
            ImGui::TextColored(record.waitMs > record.runMs ? kColQueue : kColDim,
                "%.1fms", record.waitMs);

            ImGui::TableNextColumn();
            ImGui::TextColored(kColValue, "%.1fms", record.runMs);

            ImGui::TableNextColumn();
            ImGui::TextColored(kColDim, "%.0fms", record.startMs);
        }

        ImGui::EndTable();
    }

} // namespace CoreEngine

#endif // USE_IMGUI
