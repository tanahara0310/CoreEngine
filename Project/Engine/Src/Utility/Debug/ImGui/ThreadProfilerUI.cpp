#include "ThreadProfilerUI.h"

#ifdef USE_IMGUI

#include "Threading/ThreadPool.h"
#include "Utility/Debug/ImGui/ImGuiAll.h"

#include <algorithm>
#include <format>

namespace CoreEngine
{

    // ─── パレット ────────────────────────────────────────────────────────────
    static constexpr ImVec4 kColActive = { 0.25f, 0.88f, 0.42f, 1.0f }; // 緑   : 実行中
    static constexpr ImVec4 kColIdle = { 0.20f, 0.20f, 0.24f, 1.0f }; // 暗灰 : 待機中
    static constexpr ImVec4 kColQueued = { 0.95f, 0.68f, 0.15f, 1.0f }; // 琥珀 : キュー待ち
    static constexpr ImVec4 kColHdr = { 0.12f, 0.20f, 0.34f, 0.92f };
    static constexpr ImVec4 kColHdrHov = { 0.16f, 0.28f, 0.46f, 0.96f };
    static constexpr ImVec4 kColFrameBg = { 0.10f, 0.10f, 0.12f, 1.0f };
    static constexpr ImVec4 kColGraphAct = { 0.35f, 0.78f, 0.95f, 1.0f }; // 水色 : アクティブ推移
    static constexpr ImVec4 kColDim = { 0.55f, 0.55f, 0.58f, 1.0f }; // 補足テキスト
    static constexpr ImVec4 kColWhite = { 0.90f, 0.90f, 0.92f, 1.0f };

    static ImVec4 LerpCol(const ImVec4& a, const ImVec4& b, float t)
    {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                 a.z + (b.z - a.z) * t, 1.0f };
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::RegisterPool(const std::string& name, std::function<ThreadPool* ()> getter)
    {
        for (auto& e : pools_) {
            if (e.name == name) { e.getter = std::move(getter); return; }
        }
        PoolEntry e;
        e.name = name;
        e.getter = std::move(getter);
        pools_.push_back(std::move(e));
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::Draw()
    {
        if (pools_.empty()) {
            ImGui::TextDisabled("スレッドプールが登録されていません");
            return;
        }

        for (auto& entry : pools_) {
            DrawPool(entry);
        }

        // ─── 凡例（パネル末尾に1回だけ） ─────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 各エントリに固有IDを付与して ID 衝突を防ぐ
        struct LegendDef { const char* id; ImVec4 col; const char* text; };
        const LegendDef items[] = {
            { "##lgd_act", kColActive, "実行中"     },
            { "##lgd_idl", kColIdle,   "待機中"     },
            { "##lgd_que", kColQueued, "キュー待ち" },
        };
        for (int k = 0; k < 3; ++k) {
            if (k > 0) ImGui::SameLine(0.0f, 14.0f);
            ImGui::ColorButton(items[k].id, items[k].col,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                ImVec2(12.0f, 12.0f));
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(items[k].text);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void ThreadProfilerUI::DrawPool(PoolEntry& entry)
    {
        ThreadPool* pool = entry.getter ? entry.getter() : nullptr;
        if (!pool) {
            ImGui::TextColored(kColDim, "[%s]  未初期化", entry.name.c_str());
            return;
        }

        // ID をプール名でスコープし、同一ウィジェット ID の衝突を防ぐ
        ImGui::PushID(entry.name.c_str());

        const auto     stats = pool->GetStats();
        const uint32_t wc = stats.workerCount;
        const float    util = wc > 0
            ? static_cast<float>(stats.activeTasks) / static_cast<float>(wc) : 0.0f;
        const uint64_t remaining = stats.totalSubmitted > stats.totalCompleted
            ? stats.totalSubmitted - stats.totalCompleted : 0ULL;

        // ピーク更新
        entry.peakActive = (std::max)(entry.peakActive, stats.activeTasks);

        // 履歴更新
        entry.activeHistory[entry.historyOffset] = static_cast<float>(stats.activeTasks);
        entry.pendingHistory[entry.historyOffset] = static_cast<float>(stats.pendingTasks);
        entry.historyOffset = (entry.historyOffset + 1) % kHistorySize;

        // ─── ヘッダー ─────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Header, kColHdr);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kColHdrHov);
        const std::string hdrLabel =
            std::format("{}  - {}ワーカー##hdr", entry.name, wc);
        const bool open = ImGui::CollapsingHeader(hdrLabel.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);
        if (!open) { ImGui::PopID(); return; }

        ImGui::Indent(8.0f);

        // ─── 使用率バー ───────────────────────────────────────────
        ImGui::TextUnformatted("スレッド使用率");
        ImGui::SameLine();
        ImGui::TextColored(kColDim, "  実行中ワーカー / 総ワーカー");

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, LerpCol(kColIdle, kColActive, util));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kColFrameBg);
        const std::string utilOverlay =
            std::format("{:.0f}%  ({} / {})", util * 100.0f, stats.activeTasks, wc);
        ImGui::ProgressBar(util, ImVec2(-1.0f, 22.0f), utilOverlay.c_str());
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // ─── ワーカー状態グリッド ──────────────────────────────────
        ImGui::TextUnformatted("ワーカー状態");
        ImGui::SameLine();
        ImGui::TextColored(kColDim, "  1マス = 1スレッド");

        constexpr float kBoxSz = 30.0f;
        constexpr float kPad = 4.0f;
        const int perRow = (std::max)(1,
            static_cast<int>((ImGui::GetContentRegionAvail().x + kPad) / (kBoxSz + kPad)));

        for (uint32_t i = 0; i < wc; ++i) {
            if (i % static_cast<uint32_t>(perRow) != 0) ImGui::SameLine(0.0f, kPad);

            const bool   busy = (i < stats.workerBusy.size()) && stats.workerBusy[i];
            const ImVec4 bgCol = busy ? kColActive : kColIdle;
            const ImVec4 fgCol = busy
                ? ImVec4(0.02f, 0.08f, 0.02f, 1.0f)   // 暗緑（視認性のため）
                : ImVec4(0.50f, 0.50f, 0.54f, 1.0f);  // 中グレー

            ImGui::PushStyleColor(ImGuiCol_Button,
                bgCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(bgCol.x * 1.25f, bgCol.y * 1.25f, bgCol.z * 1.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgCol);
            ImGui::PushStyleColor(ImGuiCol_Text, fgCol);

            // ラベルの先頭部分が表示テキスト、## 以降がImGui 内部ID
            ImGui::Button(std::format("W{}##w{}", i, i).c_str(), ImVec2(kBoxSz, kBoxSz));
            ImGui::PopStyleColor(4);

            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(busy ? kColActive : kColDim,
                    "Worker %u", i);
                ImGui::Separator();
                ImGui::Text("状態 : %s", busy ? "実行中" : "待機中");
                ImGui::EndTooltip();
            }
        }

        ImGui::Spacing();

        // ─── 統計テーブル ─────────────────────────────────────────
        constexpr ImGuiTableFlags tblFlags =
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchSame;

        if (ImGui::BeginTable("##stats", 2, tblFlags)) {
            ImGui::TableSetupColumn("項目", ImGuiTableColumnFlags_WidthFixed, 145.0f);
            ImGui::TableSetupColumn("値", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // ラムダ: 1行描画
            auto row = [](const char* label, const std::string& value,
                const ImVec4& valCol) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kColDim, "%s", label);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(valCol, "%s", value.c_str());
                };

            const float peakUtil = wc > 0
                ? static_cast<float>(entry.peakActive) / static_cast<float>(wc) * 100.0f
                : 0.0f;

            row("実行中タスク数",
                std::format("{} タスク", stats.activeTasks),
                stats.activeTasks > 0 ? kColActive : kColDim);

            row("キュー待ちタスク数",
                std::format("{} タスク", stats.pendingTasks),
                stats.pendingTasks > 0 ? kColQueued : kColDim);

            row("現在の使用率",
                std::format("{:.1f}%", util * 100.0f),
                LerpCol(kColDim, kColActive, util));

            row("ピーク使用率",
                std::format("{:.1f}%  ({} / {})", peakUtil, entry.peakActive, wc),
                kColActive);

            row("総投入タスク数",
                std::format("{}", stats.totalSubmitted),
                kColWhite);

            row("総完了タスク数",
                std::format("{}", stats.totalCompleted),
                kColWhite);

            row("未完了タスク数",
                std::format("{}", remaining),
                remaining > 0 ? kColQueued : kColDim);

            ImGui::EndTable();
        }

        ImGui::Spacing();

        // ─── タイムライングラフ ───────────────────────────────────
        const float gW = ImGui::GetContentRegionAvail().x;
        constexpr float gH = 52.0f;
        const float maxActive = static_cast<float>(wc > 0 ? wc : 1);

        ImGui::TextUnformatted("実行中タスク数の推移（過去 180 フレーム）");
        ImGui::PushStyleColor(ImGuiCol_PlotLines, kColGraphAct);
        ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, ImVec4(0.6f, 0.92f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kColFrameBg);
        ImGui::PlotLines("##gact",
            entry.activeHistory.data(), kHistorySize, entry.historyOffset,
            nullptr, 0.0f, maxActive, ImVec2(gW, gH));
        ImGui::PopStyleColor(3);

        ImGui::Spacing();

        ImGui::TextUnformatted("キュー待ちタスク数の推移（過去 180 フレーム）");
        ImGui::PushStyleColor(ImGuiCol_PlotLines, kColQueued);
        ImGui::PushStyleColor(ImGuiCol_PlotLinesHovered, ImVec4(1.0f, 0.88f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kColFrameBg);
        ImGui::PlotLines("##gque",
            entry.pendingHistory.data(), kHistorySize, entry.historyOffset,
            nullptr, 0.0f, 16.0f, ImVec2(gW, gH));
        ImGui::PopStyleColor(3);

        ImGui::Unindent(8.0f);
        ImGui::Spacing();

        ImGui::PopID(); // PushID(entry.name.c_str()) に対応
    }

} // namespace CoreEngine

#endif // USE_IMGUI
