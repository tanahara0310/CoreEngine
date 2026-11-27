#include "View/FrameStripView.h"

#include "Text/TextFormat.h"
#include "View/DrawHelpers.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        /// @brief 棒の高さの上限（目標の時間の何倍までを描くか）
        constexpr double kBarCapRatio = 2.4;

        /// @brief ホイールの 1 目盛りで帯を動かすフレームの数
        constexpr int kWheelFrames = 30;

        /// @brief 選べる目標
        constexpr std::array<int, 3> kTargetFps = { 30, 60, 120 };

        /// @brief 目標の言葉（"60 fps（16.7 ms）"）
        std::string TargetLabel(int fps)
        {
            return std::format("{} fps（{:.1f} ms）", fps, 1000.0 / fps);
        }

        /// @brief フレームの棒の色
        ImVec4 LevelColor(FrameLevel level, const ImVec4& normal)
        {
            switch (level) {
            case FrameLevel::Over: return Theme::kWarm;
            case FrameLevel::Far: return Theme::kError;
            default: return normal;
            }
        }

        /// @brief 時間を棒の高さの位置（画面の y）にする
        float BarY(int64_t time, double cap, float bottom, float height)
        {
            const double ratio = std::min(static_cast<double>(time), cap) / cap;
            return bottom - static_cast<float>(ratio) * height;
        }

        /// @brief "{:.1f}" のミリ秒
        std::string Ms1(int64_t nanoseconds)
        {
            return std::format("{:.1f}", static_cast<double>(nanoseconds) / 1'000'000.0);
        }
    }

    void FrameStripView::Draw(const FrameSnapshot& frames, ViewState& view, bool live)
    {
        DrawHeader(frames, view, live);
        if (frames.Count() == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted("記録にフレームの区切り（FrameMark）がまだありません。");
            ImGui::PopStyleColor();
            return;
        }
        const float labelWidth = ImGui::CalcTextSize("33.3 ms").x + ImGui::GetStyle().ItemSpacing.x;
        DrawOverview(frames, view, labelWidth);
        DrawBars(frames, view, labelWidth);
    }

    void FrameStripView::DrawHeader(const FrameSnapshot& frames, ViewState& view, bool live)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        const float gap = style.ItemSpacing.x * 2.5f;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("フレーム");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(Theme::kAccentHover, "仕事", style.FramePadding.y);
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(Draw::WaitBarColor(), "待ち（GPU・垂直同期）", style.FramePadding.y);
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(Theme::kWarm, "目標を超えた", style.FramePadding.y);
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(Theme::kError, "目標の 2 倍を超えた", style.FramePadding.y);

        // 右端：目標を超えた数・目標・最新へ
        const size_t first = std::min(view.stripFirst, frames.Count());
        const size_t last = std::min(frames.Count(), first + kStripFrames);
        size_t over = 0;
        for (size_t i = first; i < last; ++i) {
            if (frames.IsComplete(i) && FrameLevelOf(frames.At(i).Duration(), view.TargetNs()) != FrameLevel::Normal) {
                ++over;
            }
        }
        const std::string before = std::format("表示中の {} フレームで目標を超えたのは ", Text::FormatCount(last - first));
        const std::string count = Text::FormatCount(over);
        const char* after = " フレーム";
        const std::string current = TargetLabel(view.targetFps);
        const float comboWidth = ImGui::CalcTextSize(TargetLabel(120).c_str()).x + ImGui::GetFrameHeight() + style.FramePadding.x * 2.0f;
        const bool showLatest = live && !view.followLatest;
        const char* latestLabel = "最新へ";

        float width = ImGui::CalcTextSize(before.c_str()).x + ImGui::CalcTextSize(count.c_str()).x + ImGui::CalcTextSize(after).x
            + gap + ImGui::CalcTextSize("目標").x + style.ItemInnerSpacing.x + comboWidth;
        if (showLatest) {
            width += style.ItemSpacing.x + ImGui::CalcTextSize(latestLabel).x + style.FramePadding.x * 2.0f;
        }
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + gap, right - width));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted(before.c_str());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, over > 0 ? Theme::kWarm : Theme::kText);
        ImGui::TextUnformatted(count.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(after);
        ImGui::SameLine(0.0f, gap);
        ImGui::TextUnformatted("目標");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::BeginCombo("##target", current.c_str())) {
            for (const int fps : kTargetFps) {
                if (ImGui::Selectable(TargetLabel(fps).c_str(), fps == view.targetFps)) {
                    view.targetFps = fps;
                }
            }
            ImGui::EndCombo();
        }
        if (showLatest) {
            ImGui::SameLine();
            if (ImGui::Button(latestLabel)) {
                view.followLatest = true;
            }
            ImGui::SetItemTooltip("受け取っている最新のフレームを選び続けます。");
        }
    }

    void FrameStripView::DrawOverview(const FrameSnapshot& frames, ViewState& view, float labelWidth)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const float height = lineHeight * 1.5f;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - labelWidth);
        const ImVec2 min(start.x + labelWidth, start.y);
        const ImVec2 max(min.x + width, min.y + height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddText(ImVec2(start.x, start.y + (height - lineHeight) * 0.5f), ImGui::GetColorU32(Theme::kTextDim), "全体");

        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton("##overview", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        const size_t count = frames.Count();
        const int64_t target = view.TargetNs();
        const double cap = static_cast<double>(target) * kBarCapRatio;
        const size_t maxBuckets = std::max<size_t>(1, static_cast<size_t>(width / 2.0f));
        const size_t perBucket = (count + maxBuckets - 1) / maxBuckets;
        const size_t buckets = (count + perBucket - 1) / perBucket;
        const float bucketWidth = width / static_cast<float>(buckets);

        drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::kField));
        size_t worst = 0;
        int64_t worstTime = -1;
        for (size_t b = 0; b < buckets; ++b) {
            int64_t longest = 0;
            for (size_t i = b * perBucket; i < std::min(count, (b + 1) * perBucket); ++i) {
                if (!frames.IsComplete(i)) {
                    continue;
                }
                const int64_t duration = frames.At(i).Duration();
                longest = std::max(longest, duration);
                if (duration > worstTime) {
                    worstTime = duration;
                    worst = i;
                }
            }
            const float x0 = min.x + static_cast<float>(b) * bucketWidth;
            const float top = BarY(longest, cap, max.y - 1.0f, height - 2.0f);
            const ImVec4 color = LevelColor(FrameLevelOf(longest, target), Theme::FromSrgb(63, 93, 128));
            drawList->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + std::max(1.0f, bucketWidth), max.y - 1.0f), ImGui::GetColorU32(color));
        }

        // 表示中の 300 フレームの枠
        const float scale = width / static_cast<float>(count);
        float windowLeft = min.x + static_cast<float>(view.stripFirst) * scale;
        float windowRight = min.x + static_cast<float>(std::min(count, view.stripFirst + kStripFrames)) * scale;
        if (windowRight - windowLeft < 3.0f) {
            windowRight = windowLeft + 3.0f;
        }
        drawList->AddRectFilled(ImVec2(windowLeft, min.y), ImVec2(windowRight, max.y), ImGui::GetColorU32(Theme::WithAlpha(Theme::kText, 0.10f)));
        drawList->AddRect(ImVec2(windowLeft, min.y), ImVec2(windowRight, max.y), ImGui::GetColorU32(Theme::kText));
        drawList->AddRect(min, max, ImGui::GetColorU32(Theme::kOutline));

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (active) {
            const float ratio = std::clamp((mouse.x - min.x) / width, 0.0f, 1.0f);
            const size_t center = static_cast<size_t>(ratio * static_cast<float>(count));
            view.stripFirst = std::min(center > kStripFrames / 2 ? center - kStripFrames / 2 : 0, MaxStripFirst(frames));
            view.followLatest = false;
        } else if (hovered) {
            const size_t b = std::min(buckets - 1, static_cast<size_t>(std::max(0.0f, mouse.x - min.x) / bucketWidth));
            const size_t from = b * perBucket;
            const size_t to = std::min(count, from + perBucket) - 1;
            int64_t longest = 0;
            for (size_t i = from; i <= to; ++i) {
                longest = std::max(longest, frames.At(i).Duration());
            }
            if (ImGui::BeginTooltip()) {
                if (from == to) {
                    ImGui::Text("フレーム %s　%s ms", Text::FormatCount(frames.Number(from)).c_str(), Text::FormatMs(static_cast<double>(longest)).c_str());
                } else {
                    ImGui::Text("フレーム %s〜%s　いちばん長いのは %s ms", Text::FormatCount(frames.Number(from)).c_str(),
                        Text::FormatCount(frames.Number(to)).c_str(), Text::FormatMs(static_cast<double>(longest)).c_str());
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::TextUnformatted("クリックかドラッグで、下に並べる 300 フレームを動かします。");
                ImGui::PopStyleColor();
                ImGui::EndTooltip();
            }
        }

        // 下の札：最初と最後のフレームの番号・いちばん長いフレーム
        const float labelTop = max.y + 1.0f;
        const ImU32 dim = ImGui::GetColorU32(Theme::kTextDim);
        const std::string firstLabel = Text::FormatCount(frames.Number(0));
        const std::string lastLabel = Text::FormatCount(frames.Number(count - 1));
        const float lastWidth = ImGui::CalcTextSize(lastLabel.c_str()).x;
        drawList->AddText(ImVec2(min.x, labelTop), dim, firstLabel.c_str());
        drawList->AddText(ImVec2(max.x - lastWidth, labelTop), dim, lastLabel.c_str());
        if (worstTime > 0) {
            const FrameLevel level = FrameLevelOf(worstTime, target);
            const std::string worstLabel = std::format("最大 {} ms（フレーム {}）", Text::FormatMs(static_cast<double>(worstTime)),
                Text::FormatCount(frames.Number(worst)));
            const float labelWidthPx = ImGui::CalcTextSize(worstLabel.c_str()).x;
            const float markX = min.x + (static_cast<float>(worst) + 0.5f) * scale;
            const float leftLimit = min.x + ImGui::CalcTextSize(firstLabel.c_str()).x + lineHeight;
            const float rightLimit = max.x - lastWidth - lineHeight - labelWidthPx;
            const float x = std::clamp(markX - labelWidthPx * 0.5f, leftLimit, std::max(leftLimit, rightLimit));
            const ImU32 color = ImGui::GetColorU32(LevelColor(level, Theme::kTextDim));
            drawList->AddLine(ImVec2(markX, max.y), ImVec2(markX, labelTop + lineHeight * 0.3f), color, 1.5f);
            drawList->AddText(ImVec2(x, labelTop), color, worstLabel.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, labelTop));
        ImGui::Dummy(ImVec2(labelWidth + width, lineHeight));
    }

    void FrameStripView::DrawBars(const FrameSnapshot& frames, ViewState& view, float labelWidth)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const float height = lineHeight * 7.5f;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - labelWidth);
        const ImVec2 min(start.x + labelWidth, start.y);
        const ImVec2 max(min.x + width, min.y + height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton("##bars", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        const size_t count = frames.Count();
        const int64_t target = view.TargetNs();
        const double cap = static_cast<double>(target) * kBarCapRatio;
        const float bottom = max.y - 1.0f;
        const float barHeight = height - 2.0f;
        const float slotWidth = width / static_cast<float>(kStripFrames);
        const float gap = slotWidth >= 3.0f ? 1.0f : 0.0f;
        const size_t first = std::min(view.stripFirst, count);
        const size_t shown = std::min(kStripFrames, count - first);

        // 左の目盛り
        const ImU32 dim = ImGui::GetColorU32(Theme::kTextDim);
        const auto axisLabel = [&](const std::string& text, float y) {
            const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
            drawList->AddText(ImVec2(min.x - textWidth - ImGui::GetStyle().ItemSpacing.x, y - lineHeight * 0.5f), dim, text.c_str());
        };
        axisLabel(Ms1(target * 2), BarY(target * 2, cap, bottom, barHeight));
        axisLabel(Ms1(target), BarY(target, cap, bottom, barHeight));
        axisLabel("0 ms", bottom - lineHeight * 0.5f);

        drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::kField));
        const ImU32 waitColor = ImGui::GetColorU32(Draw::WaitBarColor());
        for (size_t k = 0; k < shown; ++k) {
            const size_t index = first + k;
            const FrameTime& frame = frames.At(index);
            const int64_t duration = frame.Duration();
            const float x0 = min.x + static_cast<float>(k) * slotWidth + gap;
            const float x1 = std::max(x0 + 1.0f, min.x + static_cast<float>(k + 1) * slotWidth);
            ImVec4 workColor = LevelColor(FrameLevelOf(duration, target), Theme::kAccentHover);
            if (!frames.IsComplete(index)) {
                workColor.w = 0.5f;
            }
            const float workTop = BarY(frame.Work(), cap, bottom, barHeight);
            const float totalTop = BarY(duration, cap, bottom, barHeight);
            drawList->AddRectFilled(ImVec2(x0, workTop), ImVec2(x1, bottom), ImGui::GetColorU32(workColor));
            if (frame.wait > 0 && totalTop < workTop) {
                drawList->AddRectFilled(ImVec2(x0, totalTop), ImVec2(x1, workTop), waitColor);
            }
            if (static_cast<double>(duration) > cap) {
                const float tip = std::min(x1 - x0, lineHeight * 0.4f) + 1.0f;
                drawList->AddTriangleFilled(ImVec2(x0, min.y + 1.0f), ImVec2(x1, min.y + 1.0f),
                    ImVec2((x0 + x1) * 0.5f, min.y + 1.0f + tip), ImGui::GetColorU32(Theme::kText));
            }
        }

        // 目標の線と 2 倍の線
        const float dash = lineHeight * 0.4f;
        const float targetY = BarY(target, cap, bottom, barHeight);
        const float farY = BarY(target * 2, cap, bottom, barHeight);
        Draw::DashedHLine(drawList, min.x, max.x, targetY, ImGui::GetColorU32(Theme::kOk), dash, dash * 0.6f);
        Draw::DashedHLine(drawList, min.x, max.x, farY, ImGui::GetColorU32(Theme::kError), dash, dash * 0.6f);
        const std::string targetText = "目標 " + Ms1(target) + " ms";
        const ImVec2 targetSize = ImGui::CalcTextSize(targetText.c_str());
        const ImVec2 targetPos(max.x - targetSize.x - lineHeight * 0.4f, targetY - targetSize.y - 1.0f);
        drawList->AddRectFilled(ImVec2(targetPos.x - 3.0f, targetPos.y), ImVec2(targetPos.x + targetSize.x + 3.0f, targetY - 1.0f),
            ImGui::GetColorU32(Theme::kField));
        drawList->AddText(targetPos, ImGui::GetColorU32(Theme::kOk), targetText.c_str());
        drawList->AddRect(min, max, ImGui::GetColorU32(Theme::kOutline));

        // 選んだフレーム
        if (view.hasSelectedFrame && view.selectedFrame >= first && view.selectedFrame < first + shown) {
            const size_t k = view.selectedFrame - first;
            const float x0 = min.x + static_cast<float>(k) * slotWidth;
            const float x1 = x0 + slotWidth;
            drawList->AddRect(ImVec2(x0 - 1.0f, min.y + 1.0f), ImVec2(x1 + 1.0f, max.y - 1.0f), ImGui::GetColorU32(Theme::kText), 0.0f, 0, 1.5f);
            const FrameTime& frame = frames.At(view.selectedFrame);
            const std::string label = std::format("{}　{} ms", Text::FormatCount(frames.Number(view.selectedFrame)), Ms1(frame.Duration()));
            const ImVec2 size = ImGui::CalcTextSize(label.c_str());
            const float padding = lineHeight * 0.35f;
            const float left = std::clamp((x0 + x1) * 0.5f - size.x * 0.5f - padding, min.x + 1.0f, max.x - size.x - padding * 2.0f - 1.0f);
            const float top = min.y + lineHeight * 0.3f;
            drawList->AddRectFilled(ImVec2(left, top), ImVec2(left + size.x + padding * 2.0f, top + size.y + 2.0f), ImGui::GetColorU32(Theme::kText));
            drawList->AddText(ImVec2(left + padding, top + 1.0f), ImGui::GetColorU32(Theme::kDeepest), label.c_str());
        }

        // マウスを重ねたフレーム
        if (hovered && shown > 0) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const size_t k = static_cast<size_t>(std::max(0.0f, mouse.x - min.x) / slotWidth);
            if (k < shown) {
                const size_t index = first + k;
                const float x0 = min.x + static_cast<float>(k) * slotWidth;
                drawList->AddRectFilled(ImVec2(x0, min.y + 1.0f), ImVec2(x0 + slotWidth, max.y - 1.0f),
                    ImGui::GetColorU32(Theme::WithAlpha(Theme::kText, 0.12f)));
                const FrameTime& frame = frames.At(index);
                if (ImGui::BeginTooltip()) {
                    ImGui::Text("フレーム %s　%s ms", Text::FormatCount(frames.Number(index)).c_str(),
                        Text::FormatMs(static_cast<double>(frame.Duration())).c_str());
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                    if (frame.wait >= 0) {
                        ImGui::Text("仕事 %s ms・待ち %s ms", Text::FormatMs(static_cast<double>(frame.Work())).c_str(),
                            Text::FormatMs(static_cast<double>(frame.wait)).c_str());
                    }
                    if (!frames.IsComplete(index)) {
                        ImGui::TextUnformatted("まだ終わっていないフレームです。");
                    }
                    ImGui::TextUnformatted("クリックで、このフレームをタイムラインに出します。");
                    ImGui::PopStyleColor();
                    ImGui::EndTooltip();
                }
                if (clicked) {
                    SelectFrame(view, frames, index);
                    view.followLatest = false;
                }
            }
        }
        if (hovered) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                const int64_t moved = static_cast<int64_t>(view.stripFirst) - static_cast<int64_t>(wheel * kWheelFrames);
                view.stripFirst = static_cast<size_t>(std::clamp<int64_t>(moved, 0, static_cast<int64_t>(MaxStripFirst(frames))));
                view.followLatest = false;
            }
        }
    }
}
