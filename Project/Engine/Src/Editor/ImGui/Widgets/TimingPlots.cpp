#include "pch.h"
#include "Editor/ImGui/Widgets/TimingPlots.h"

#include "Editor/ImGui/EditorTheme.h"

#include <implot.h>

#include <algorithm>
#include <cmath>

namespace CoreEngine::UI
{
    namespace
    {
        namespace Theme = Editor::Theme;

        constexpr int kFrames = TimingHistory::kFrames;
        constexpr int kGroupCount = TimingHistory::kGroupCount;

        /// まとまりの名前（下から積む順）
        constexpr const char* kGroupLabels[kGroupCount] = {
            "影", "G-Buffer", "ライティング", "空・雲", "透明", "水面", "ポスト処理", "その他",
        };

        /// グラフの操作は値の確かめだけにする（拡大・移動・メニュー・マウスの座標の表示は出さない）
        constexpr ImPlotFlags kPlotFlags = ImPlotFlags_NoMouseText | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;
        constexpr ImPlotAxisFlags kFrameAxisFlags = ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Lock;
        constexpr ImPlotAxisFlags kTimeAxisFlags = ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus | ImPlotAxisFlags_NoHighlight;

        /// @brief カテゴリを積み上げのまとまりの番号にする（積まないカテゴリは -1）
        int GroupOf(GpuTimingCategory category)
        {
            switch (category) {
            case GpuTimingCategory::Shadow:       return 0;
            case GpuTimingCategory::GBuffer:      return 1;
            case GpuTimingCategory::PreLighting:
            case GpuTimingCategory::Lighting:
            case GpuTimingCategory::PostLighting: return 2;
            case GpuTimingCategory::Sky:          return 3;
            case GpuTimingCategory::Transparent:  return 4;
            case GpuTimingCategory::Water:        return 5;
            case GpuTimingCategory::PostProcess:  return 6;
            case GpuTimingCategory::Setup:
            case GpuTimingCategory::Overlay:
            case GpuTimingCategory::Final:
            case GpuTimingCategory::Editor:       return 7;
            default:                              return -1;
            }
        }

        /// @brief 履歴を古い順に並べ直し、入っている数を返す
        int Unroll(const RingHistory<float, kFrames>& history, float* out)
        {
            const int count = history.Count();
            for (int i = 0; i < count; ++i) {
                out[i] = history.At(i);
            }
            return count;
        }

        /// @brief 横の位置（最新のフレームを 0、古いフレームほど負）を並べる
        void FillFramePositions(int count, float* out)
        {
            for (int i = 0; i < count; ++i) {
                out[i] = static_cast<float>(i - (count - 1));
            }
        }

        /// @brief 縦の上限（最大値の少し上を 5 ms ごとに切り上げる）
        double UpperLimit(float maxMs)
        {
            const double step = 5.0;
            return (std::max)(step, std::ceil(static_cast<double>(maxMs) * 1.1 / step) * step);
        }

        /// @brief 横軸と縦軸を決める（横は直近のフレーム、縦は 0 から上限まで。どちらも動かせない）
        void SetupTimeAxes(double upperMs)
        {
            ImPlot::SetupAxes(nullptr, nullptr, kFrameAxisFlags, kTimeAxisFlags);
            ImPlot::SetupAxisLimits(ImAxis_X1, -(kFrames - 1) - 0.5, 0.5, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, upperMs, ImPlotCond_Always);
            ImPlot::SetupAxisFormat(ImAxis_Y1, "%g ms");
        }

        /// @brief マウスの下のフレーム（古い順の番号。グラフの外なら -1）
        int HoveredIndex(int count)
        {
            if (count == 0 || !ImPlot::IsPlotHovered()) {
                return -1;
            }
            const double x = ImPlot::GetPlotMousePos().x;
            const int index = (count - 1) + static_cast<int>(std::lround(x));
            return (index >= 0 && index < count) ? index : -1;
        }

        /// @brief マウスの下のフレームに縦線を引く
        void DrawHoverLine(float x)
        {
            ImPlotSpec spec;
            spec.LineColor = Theme::kTextMute;
            spec.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
            const double position = x;
            ImPlot::PlotInfLines("##hover", &position, 1, spec);
        }

        /// @brief 吹き出しを開き、見出し（何フレーム前か）と値の表を始める
        bool BeginHoverTooltip(int framesAgo)
        {
            ImGui::BeginTooltip();
            if (framesAgo == 0) {
                ImGui::TextColored(Theme::kTextDim, "最新のフレーム");
            } else {
                ImGui::TextColored(Theme::kTextDim, "%d フレーム前", framesAgo);
            }
            if (ImGui::BeginTable("##hover_values", 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("##name");
                ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.00 ms").x);
                return true;
            }
            ImGui::EndTooltip();
            return false;
        }

        /// @brief 値の表と吹き出しを閉じる
        void EndHoverTooltip()
        {
            ImGui::EndTable();
            ImGui::EndTooltip();
        }

        /// @brief 吹き出しの 1 行（系列の色の四角と名前・右寄せの値）
        void HoverRow(const ImVec4& color, const char* label, float ms)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const float lineHeight = ImGui::GetTextLineHeight();
            const float size = lineHeight * 0.6f;
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float top = cursor.y + (lineHeight - size) * 0.5f;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(cursor.x, top), ImVec2(cursor.x + size, top + size), ImGui::GetColorU32(color), 2.0f);
            ImGui::Dummy(ImVec2(size, lineHeight));
            ImGui::SameLine();
            ImGui::TextColored(Theme::kText, "%s", label);

            ImGui::TableSetColumnIndex(1);
            char text[32];
            snprintf(text, sizeof(text), "%.2f ms", ms);
            const float textWidth = ImGui::CalcTextSize(text).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x - textWidth));
            ImGui::TextColored(Theme::kText, "%s", text);
        }
    }

    void TimingHistory::Push(float frameMs, const TimingSlots* gpuSlots)
    {
        std::array<float, kGroupCount> groups{};
        float totalMs = 0.0f;
        if (gpuSlots) {
            hasGpu_ = true;
            for (const GpuTimingResult& slot : *gpuSlots) {
                if (slot.category == GpuTimingCategory::Frame) {
                    totalMs = slot.gpuMs;
                    continue;
                }
                const int group = GroupOf(slot.category);
                if (group >= 0) {
                    groups[static_cast<size_t>(group)] += slot.gpuMs;
                }
            }
        }

        frameMs_.Push(frameMs);
        gpuMs_.Push(totalMs);
        for (int i = 0; i < kGroupCount; ++i) {
            groupMs_[static_cast<size_t>(i)].Push(groups[static_cast<size_t>(i)]);
        }
    }

    void TimingHistory::DrawFrameTimePlot(const char* id, float targetFps, float height) const
    {
        std::array<float, kFrames> positions{};
        std::array<float, kFrames> frame{};
        std::array<float, kFrames> gpu{};
        const int count = Unroll(frameMs_, frame.data());
        Unroll(gpuMs_, gpu.data());
        FillFramePositions(count, positions.data());

        const float targetMs = targetFps > 0.0f ? 1000.0f / targetFps : 0.0f;
        float maxMs = targetMs;
        for (int i = 0; i < count; ++i) {
            maxMs = (std::max)({ maxMs, frame[static_cast<size_t>(i)], hasGpu_ ? gpu[static_cast<size_t>(i)] : 0.0f });
        }

        if (!ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
            return;
        }
        SetupTimeAxes(UpperLimit(maxMs));
        ImPlot::SetupLegend(ImPlotLocation_North, ImPlotLegendFlags_Outside | ImPlotLegendFlags_Horizontal);

        ImPlotSpec line;
        line.LineWeight = 2.0f;
        line.LineColor = Theme::kSeries[0];
        ImPlot::PlotLine("フレーム", positions.data(), frame.data(), count, line);
        if (hasGpu_) {
            line.LineColor = Theme::kSeries[1];
            ImPlot::PlotLine("GPU", positions.data(), gpu.data(), count, line);
        }

        // 目標の横線は線の上に重ねる
        if (targetMs > 0.0f) {
            ImPlotSpec target;
            target.LineColor = Theme::kTextDim;
            target.Flags = static_cast<ImPlotItemFlags>(ImPlotInfLinesFlags_Horizontal) | ImPlotItemFlags_NoFit;
            char label[48];
            snprintf(label, sizeof(label), "目標 %.0f fps###target", targetFps);
            const double targetY = targetMs;
            ImPlot::PlotInfLines(label, &targetY, 1, target);
        }

        const int hovered = HoveredIndex(count);
        if (hovered >= 0) {
            const size_t i = static_cast<size_t>(hovered);
            DrawHoverLine(positions[i]);
            if (BeginHoverTooltip(count - 1 - hovered)) {
                HoverRow(Theme::kSeries[0], "フレーム", frame[i]);
                if (hasGpu_) {
                    HoverRow(Theme::kSeries[1], "GPU", gpu[i]);
                }
                EndHoverTooltip();
            }
        }

        ImPlot::EndPlot();
    }

    void TimingHistory::DrawGpuStackPlot(const char* id, float height) const
    {
        std::array<float, kFrames> positions{};
        std::array<float, kFrames> total{};
        std::array<std::array<float, kFrames>, kGroupCount> values{};
        // edges[g] は下から g 個のまとまりを足した高さ（edges[0] は 0）
        std::array<std::array<float, kFrames>, kGroupCount + 1> edges{};

        const int count = Unroll(gpuMs_, total.data());
        FillFramePositions(count, positions.data());
        float maxMs = 0.0f;
        for (int g = 0; g < kGroupCount; ++g) {
            Unroll(groupMs_[static_cast<size_t>(g)], values[static_cast<size_t>(g)].data());
        }
        for (int i = 0; i < count; ++i) {
            const size_t f = static_cast<size_t>(i);
            for (size_t g = 0; g < kGroupCount; ++g) {
                edges[g + 1][f] = edges[g][f] + values[g][f];
            }
            maxMs = (std::max)({ maxMs, total[f], edges[kGroupCount][f] });
        }

        if (!ImPlot::BeginPlot(id, ImVec2(-1.0f, height), kPlotFlags)) {
            return;
        }
        SetupTimeAxes(UpperLimit(maxMs));
        // 凡例は上から「合計 → 上に積んだまとまり」の順にする（足した順の逆）
        ImPlot::SetupLegend(ImPlotLocation_NorthEast,
            ImPlotLegendFlags_Outside | ImPlotLegendFlags_NoButtons | ImPlotLegendFlags_Reverse);

        for (size_t g = 0; g < kGroupCount; ++g) {
            ImPlotSpec fill;
            fill.FillColor = Theme::kSeries[g];
            ImPlot::PlotShaded(kGroupLabels[g], positions.data(), edges[g].data(), edges[g + 1].data(), count, fill);
        }

        // まとまりの境目に面の色の線を引いて隙間にする
        ImPlotSpec gap;
        gap.LineColor = Theme::kField;
        gap.LineWeight = 2.0f;
        gap.Flags = ImPlotItemFlags_NoLegend | ImPlotItemFlags_NoFit;
        for (size_t g = 1; g < kGroupCount; ++g) {
            ImPlot::PlotLine("##gap", positions.data(), edges[g].data(), count, gap);
        }

        ImPlotSpec totalLine;
        totalLine.LineColor = Theme::kText;
        totalLine.LineWeight = 1.5f;
        ImPlot::PlotLine("合計", positions.data(), total.data(), count, totalLine);

        const int hovered = HoveredIndex(count);
        if (hovered >= 0) {
            const size_t i = static_cast<size_t>(hovered);
            DrawHoverLine(positions[i]);
            if (BeginHoverTooltip(count - 1 - hovered)) {
                HoverRow(Theme::kText, "合計", total[i]);
                for (size_t g = kGroupCount; g-- > 0;) {
                    HoverRow(Theme::kSeries[g], kGroupLabels[g], values[g][i]);
                }
                EndHoverTooltip();
            }
        }

        ImPlot::EndPlot();
    }
}
