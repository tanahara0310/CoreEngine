#include "pch.h"
#include "Editor/ImGui/Widgets/PassTimingTable.h"

#include "Editor/ImGui/EditorTheme.h"

#include <format>
#include <vector>

namespace CoreEngine::UI
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 1 フレームの予算（60FPS）
        constexpr float kFrameBudgetMs = 1000.0f / 60.0f;

        /// 色を変えるしきい値（ミリ秒）
        constexpr float kGpuWarnMs = 4.0f;
        constexpr float kGpuErrorMs = 8.0f;
        constexpr float kCpuWarnMs = 0.5f;
        constexpr float kCpuErrorMs = 2.0f;

        /// @brief 1 行分のセル（名前の左に状態の丸、右端に予算に対する割合のバー）
        void DrawRow(const GpuTimingResult& slot, bool idle, bool total, bool showBudget)
        {
            const ImVec4 gpuColor = total ? Theme::kWarm : GpuTimeColor(slot.gpuMs, idle);
            const ImVec4 cpuColor = total ? Theme::kWarm : CpuTimeColor(slot.cpuMs, idle);

            ImGui::TableSetColumnIndex(0);
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                const ImVec2 dot(cursor.x + 4.0f, cursor.y + ImGui::GetTextLineHeight() * 0.5f);
                ImGui::GetWindowDrawList()->AddCircleFilled(dot, 4.0f, ImGui::GetColorU32(gpuColor));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
            }
            ImGui::TextColored(total ? Theme::kWarm : (idle ? Theme::kTextMute : Theme::kText), "%s", slot.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(cpuColor, "%.3f", slot.cpuMs);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(gpuColor, "%.3f", slot.gpuMs);

            if (showBudget) {
                ImGui::TableSetColumnIndex(3);
                const float ratio = slot.gpuMs / kFrameBudgetMs < 1.0f ? slot.gpuMs / kFrameBudgetMs : 1.0f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, gpuColor);
                const std::string overlay = std::format("{:.1f}%", ratio * 100.0f);
                ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()), overlay.c_str());
                ImGui::PopStyleColor();
            }
        }
    }

    ImVec4 GpuTimeColor(float milliseconds, bool idle)
    {
        if (idle) {
            return Theme::kTextMute;
        }
        return milliseconds > kGpuErrorMs ? Theme::kError
            : milliseconds > kGpuWarnMs ? Theme::kWarn
            : Theme::kOk;
    }

    ImVec4 CpuTimeColor(float milliseconds, bool idle)
    {
        if (idle) {
            return Theme::kTextMute;
        }
        return milliseconds > kCpuErrorMs ? Theme::kError
            : milliseconds > kCpuWarnMs ? Theme::kWarn
            : Theme::kTextDim;
    }

    void PassTimingTable(const char* id, const TimingSlots& slots, bool showBudget)
    {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV
            | ImGuiTableFlags_BordersOuter
            | ImGuiTableFlags_RowBg;

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 3.0f));
        if (ImGui::BeginTable(id, showBudget ? 4 : 3, flags)) {
            ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            if (showBudget) {
                ImGui::TableSetupColumn("予算比", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            }
            ImGui::TableHeadersRow();

            // パス名から解決したカテゴリでまとめる（新しいパスを足しても表の側は変えなくてよい）
            const std::vector<GpuTimingGroup> groups = BuildGpuTimingGroups(slots);

            for (const GpuTimingGroup& group : groups) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(Theme::kWarm, "%s", GpuTimestampProfiler::GetCategoryLabel(group.category));

                for (const uint32_t index : group.slotIndices) {
                    if (index >= slots.size()) {
                        continue;
                    }
                    // 今フレーム実行されなかったパスも行の位置を保ち、淡色で出す
                    ImGui::TableNextRow();
                    DrawRow(slots[index], IsIdleTimingSlot(slots[index]), false, showBudget);
                }
            }

            // フレーム合計
            for (const GpuTimingResult& slot : slots) {
                if (slot.category != GpuTimingCategory::Frame) {
                    continue;
                }
                ImGui::TableNextRow();
                const ImU32 background = ImGui::GetColorU32(Theme::kPanel);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, background);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, background);
                DrawRow(slot, false, true, showBudget);
                break;
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
}

