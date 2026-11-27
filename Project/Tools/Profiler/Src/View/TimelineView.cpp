#include "View/TimelineView.h"

#include "Capture/FrameSnapshot.h"
#include "Capture/SourceInfoCache.h"
#include "Capture/TracyAccess.h"
#include "Text/TextFormat.h"
#include "View/DrawHelpers.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        /// @brief これより細い区間（画面の幅）は、隣の細い区間とまとめて 1 つにする
        constexpr float kMinZonePixels = 3.0f;

        /// @brief 行の中の段の数の上限
        constexpr int kMaxMainLanes = 24;
        constexpr int kMaxWorkerLanes = 8;
        constexpr int kMaxGpuLanes = 8;

        /// @brief 拡大の限度（ナノ秒）
        constexpr int64_t kMinSpan = 2'000;

        /// @brief ホイールの 1 目盛りで範囲を何倍にするか
        constexpr double kWheelZoom = 0.8;

        /// @brief 目盛りの間隔を 1・2・5 × 10 の累乗に丸める
        double NiceStep(double raw)
        {
            const double exponent = std::pow(10.0, std::floor(std::log10(std::max(raw, 1.0))));
            const double mantissa = raw / exponent;
            const double nice = mantissa <= 1.0 ? 1.0 : (mantissa <= 2.0 ? 2.0 : (mantissa <= 5.0 ? 5.0 : 10.0));
            return nice * exponent;
        }

        /// @brief 目盛りの数字（ミリ秒。間隔に合わせて小数の桁を決める）
        std::string TickLabel(double nanoseconds, double step)
        {
            const int decimals = step >= 1e6 ? 0 : (step >= 1e5 ? 1 : (step >= 1e4 ? 2 : (step >= 1e3 ? 3 : 4)));
            const double ms = std::abs(nanoseconds) < step * 0.01 ? 0.0 : nanoseconds / 1'000'000.0;
            return std::format("{:.{}f}", ms, decimals);
        }
    }

    void TimelineView::Reset()
    {
        zones_.clear();
        rows_.clear();
        merging_ = false;
        gpuFrameTime_ = -1;
        workerThreads_ = 0;
        hoverStats_ = {};
        hasHover_ = false;
    }

    void TimelineView::Update(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ViewState& view)
    {
        zones_.clear();
        rows_.clear();
        merging_ = false;
        captureStart_ = worker.GetFirstTime();
        captureEnd_ = worker.GetLastTime();
        rangeStart_ = view.viewStart;
        rangeEnd_ = view.viewEnd;
        gpuFrameTime_ = -1;
        CopyHoverStats(worker);
        if (rangeEnd_ <= rangeStart_) {
            return;
        }
        const double nsPerPixel = static_cast<double>(rangeEnd_ - rangeStart_) / std::max(1.0f, plotWidth_);
        minZoneNs_ = std::max<int64_t>(1, static_cast<int64_t>(nsPerPixel * kMinZonePixels));

        // メインスレッド
        const tracy::ThreadData* mainThread = frames.MainThread();
        rows_.push_back(Row{ RowKind::Main, "メインスレッド", mainThread ? Access::ThreadLabel(worker, mainThread->id) : std::string(), 0 });
        if (mainThread) {
            uint16_t lanes = 0;
            CollectCpu(worker, sources, mainThread->timeline, 0, 0, 0, mainThread->id, lanes);
            rows_[0].lanes = lanes;
        }

        // ワーカー（仕事のあるスレッドだけ段を割り当てる）
        workerThreads_ = 0;
        for (const tracy::ThreadData* thread : worker.GetThreadData()) {
            if (thread != mainThread && !thread->timeline.empty()) {
                ++workerThreads_;
            }
        }
        if (workerThreads_ > 0) {
            const uint16_t row = static_cast<uint16_t>(rows_.size());
            rows_.push_back(Row{ RowKind::Worker, std::format("ワーカー ×{}", workerThreads_), std::string(), 0 });
            uint16_t used = 0;
            uint32_t active = 0;
            for (const tracy::ThreadData* thread : worker.GetThreadData()) {
                if (thread == mainThread || thread->timeline.empty()) {
                    continue;
                }
                uint16_t lanes = 0;
                CollectCpu(worker, sources, thread->timeline, row, used, 0, thread->id, lanes);
                if (lanes > 0) {
                    used = static_cast<uint16_t>(used + lanes);
                    ++active;
                }
            }
            rows_[row].lanes = used;
            if (active > 0) {
                rows_[row].subtitle = std::format("仕事のあるスレッド {} 本", active);
            }
        }

        // GPU のキュー
        const auto& contexts = worker.GetGpuData();
        for (size_t c = 0; c < contexts.size(); ++c) {
            const tracy::GpuCtxData& context = *contexts[c];
            const uint16_t row = static_cast<uint16_t>(rows_.size());
            rows_.push_back(Row{ RowKind::Gpu, "GPU", Access::QueueLabel(worker, context), 0 });
            uint16_t lanes = 0;
            for (const auto& entry : context.threadData) {
                CollectGpu(worker, sources, entry.second.timeline, row, 0, c, lanes);
            }
            rows_[row].lanes = lanes;
        }

        // 選んだフレームの間に始まった GPU の区間の合計
        if (view.hasSelectedFrame && view.selectedFrame < frames.Count() && !contexts.empty()) {
            const FrameTime& frame = frames.At(view.selectedFrame);
            int64_t sum = 0;
            for (const tracy::GpuCtxData* context : contexts) {
                for (const auto& entry : context->threadData) {
                    const auto& events = entry.second.timeline;
                    for (size_t i = Access::FirstGpuEndingAfter(events, frame.begin); i < events.size(); ++i) {
                        const tracy::GpuEvent& event = Access::At(events, i);
                        const int64_t start = event.GpuStart();
                        if (start < 0) {
                            continue;
                        }
                        if (start >= frame.end) {
                            break;
                        }
                        if (start >= frame.begin) {
                            sum += worker.GetZoneEnd(event) - start;
                        }
                    }
                }
            }
            gpuFrameTime_ = sum;
        }
    }

    void TimelineView::CollectCpu(const tracy::Worker& worker, SourceInfoCache& sources,
        const tracy::Vector<tracy::short_ptr<tracy::ZoneEvent>>& events, uint16_t row, uint16_t laneBase, int depth,
        uint64_t thread, uint16_t& lanes)
    {
        const int maxLanes = (row == 0) ? kMaxMainLanes : kMaxWorkerLanes;
        const uint16_t lane = static_cast<uint16_t>(laneBase + depth);
        for (size_t i = Access::FirstEndingAfter(worker, events, rangeStart_); i < events.size(); ++i) {
            const tracy::ZoneEvent& event = Access::At(events, i);
            const int64_t start = event.Start();
            if (start > rangeEnd_) {
                break;
            }
            const int64_t end = worker.GetZoneEnd(event);
            lanes = std::max<uint16_t>(lanes, static_cast<uint16_t>(depth + 1));
            sources.Get(worker, event.SrcLoc());

            if (end - start < minZoneNs_) {
                if (merging_ && merge_.row == row && merge_.lane == lane && start - merge_.end < minZoneNs_) {
                    merge_.end = std::max(merge_.end, end);
                    ++merge_.merged;
                } else {
                    FlushMerge();
                    merge_ = Zone{ start, end, ZoneRef{ &event, nullptr, thread }, event.SrcLoc(), lane, row, 1, false };
                    merging_ = true;
                }
                continue;
            }

            FlushMerge();
            zones_.push_back(Zone{ start, end, ZoneRef{ &event, nullptr, thread }, event.SrcLoc(), lane, row, 1, (i & 1) != 0 });
            if (event.HasChildren() && depth + 1 < maxLanes) {
                CollectCpu(worker, sources, worker.GetZoneChildren(event.Child()), row, laneBase, depth + 1, thread, lanes);
            }
        }
        FlushMerge();
    }

    void TimelineView::CollectGpu(const tracy::Worker& worker, SourceInfoCache& sources,
        const tracy::Vector<tracy::short_ptr<tracy::GpuEvent>>& events, uint16_t row, int depth, uint64_t context,
        uint16_t& lanes)
    {
        const uint16_t lane = static_cast<uint16_t>(depth);
        for (size_t i = Access::FirstGpuEndingAfter(events, rangeStart_); i < events.size(); ++i) {
            const tracy::GpuEvent& event = Access::At(events, i);
            const int64_t start = event.GpuStart();
            if (start < 0) {
                continue;
            }
            if (start > rangeEnd_) {
                break;
            }
            const int64_t end = worker.GetZoneEnd(event);
            lanes = std::max<uint16_t>(lanes, static_cast<uint16_t>(depth + 1));
            sources.Get(worker, event.SrcLoc());

            if (end - start < minZoneNs_) {
                if (merging_ && merge_.row == row && merge_.lane == lane && start - merge_.end < minZoneNs_) {
                    merge_.end = std::max(merge_.end, end);
                    ++merge_.merged;
                } else {
                    FlushMerge();
                    merge_ = Zone{ start, end, ZoneRef{ nullptr, &event, context }, event.SrcLoc(), lane, row, 1, false };
                    merging_ = true;
                }
                continue;
            }

            FlushMerge();
            zones_.push_back(Zone{ start, end, ZoneRef{ nullptr, &event, context }, event.SrcLoc(), lane, row, 1, (i & 1) != 0 });
            if (event.Child() >= 0 && depth + 1 < kMaxGpuLanes) {
                CollectGpu(worker, sources, worker.GetGpuChildren(event.Child()), row, depth + 1, context, lanes);
            }
        }
        FlushMerge();
    }

    void TimelineView::FlushMerge()
    {
        if (!merging_) {
            return;
        }
        if (merge_.merged > 1) {
            merge_.ref = ZoneRef{};
        }
        zones_.push_back(merge_);
        merging_ = false;
    }

    void TimelineView::CopyHoverStats(const tracy::Worker& worker)
    {
        hoverStats_.valid = false;
        if (!hasHover_) {
            return;
        }
        hoverStats_.srcloc = hoverSrcloc_;
        hoverStats_.gpu = hoverGpu_;
        if (hoverGpu_) {
            const auto& map = worker.GetGpuSourceLocationZones();
            const auto it = map.find(hoverSrcloc_);
            if (it != map.end() && !it->second.zones.empty()) {
                hoverStats_ = HoverStats{ hoverSrcloc_, true, true, it->second.zones.size(), it->second.total, it->second.max };
            }
        } else {
            const auto& map = worker.GetSourceLocationZones();
            const auto it = map.find(hoverSrcloc_);
            if (it != map.end() && !it->second.zones.empty()) {
                hoverStats_ = HoverStats{ hoverSrcloc_, false, true, it->second.zones.size(), it->second.total, it->second.max };
            }
        }
    }

    void TimelineView::Draw(const FrameSnapshot& frames, const SourceInfoCache& sources, ViewState& view)
    {
        DrawHeader(frames, view);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kChild);
        ImGui::BeginChild("##timeline", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse);

        const float lineHeight = ImGui::GetTextLineHeight();
        const float laneHeight = std::floor(lineHeight * 1.45f);
        const float rowPadding = std::floor(lineHeight * 0.35f);
        const float labelWidth = lineHeight * 8.5f;
        const float rulerHeight = lineHeight * 1.6f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float left = origin.x + labelWidth;
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - labelWidth - lineHeight * 0.5f);
        plotWidth_ = width;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        DrawRuler(drawList, frames, view, left, origin.y, width, rulerHeight, labelWidth);

        // 行の位置
        std::vector<float> rowTop(rows_.size());
        float y = origin.y + rulerHeight;
        for (size_t r = 0; r < rows_.size(); ++r) {
            rowTop[r] = y;
            const float body = rows_[r].lanes > 0 ? static_cast<float>(rows_[r].lanes) * laneHeight : lineHeight * 1.2f;
            y += std::max(body, lineHeight * 2.2f) + rowPadding * 2.0f;
        }
        const float contentTop = origin.y + rulerHeight;
        const float contentBottom = y;
        const float right = left + width;
        const int64_t span = std::max<int64_t>(1, view.viewEnd - view.viewStart);
        const double pixelsPerNs = static_cast<double>(width) / static_cast<double>(span);
        const auto toX = [&](int64_t time) {
            return left + static_cast<float>(static_cast<double>(time - view.viewStart) * pixelsPerNs);
        };

        // 選んだフレームの下地とフレームの境目
        drawList->PushClipRect(ImVec2(left, contentTop), ImVec2(right, contentBottom), true);
        if (view.hasSelectedFrame && view.selectedFrame < frames.Count()) {
            const FrameTime& frame = frames.At(view.selectedFrame);
            drawList->AddRectFilled(ImVec2(toX(frame.begin), contentTop), ImVec2(toX(frame.end), contentBottom),
                ImGui::GetColorU32(Theme::WithAlpha(Theme::kAccentMuted, 0.45f)));
        }
        if (frames.Count() > 0) {
            float lastX = -1.0e9f;
            for (size_t i = frames.FindFrame(view.viewStart); i < frames.Count(); ++i) {
                const int64_t begin = frames.At(i).begin;
                if (begin > view.viewEnd) {
                    break;
                }
                const float x = toX(begin);
                if (x - lastX >= 4.0f) {
                    drawList->AddLine(ImVec2(x, contentTop), ImVec2(x, contentBottom), ImGui::GetColorU32(Theme::kOutline));
                    lastX = x;
                }
            }
        }
        drawList->PopClipRect();

        // 行の名前と区切り
        const ImU32 textColor = ImGui::GetColorU32(Theme::kText);
        const ImU32 dimColor = ImGui::GetColorU32(Theme::kTextDim);
        for (size_t r = 0; r < rows_.size(); ++r) {
            const Row& row = rows_[r];
            const float top = rowTop[r] + rowPadding;
            drawList->AddText(ImVec2(origin.x, top), textColor, row.title.c_str());
            if (!row.subtitle.empty()) {
                drawList->PushClipRect(ImVec2(origin.x, top), ImVec2(left - lineHeight * 0.4f, top + lineHeight * 2.2f), true);
                drawList->AddText(ImVec2(origin.x, top + lineHeight * 1.1f), dimColor, row.subtitle.c_str());
                drawList->PopClipRect();
            }
            if (row.lanes == 0) {
                const char* message = row.kind == RowKind::Worker ? "この範囲の仕事はありません" : "この範囲の区間はありません";
                drawList->AddText(ImVec2(left + lineHeight * 0.4f, top), dimColor, message);
            }
            const float bottom = (r + 1 < rows_.size()) ? rowTop[r + 1] : contentBottom;
            drawList->AddLine(ImVec2(origin.x, bottom), ImVec2(right, bottom), ImGui::GetColorU32(Theme::kBorder));
        }

        // 区間
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool plotHovered = ImGui::IsWindowHovered() && mouse.x >= left && mouse.x < right
            && mouse.y >= contentTop && mouse.y < contentBottom;
        const Zone* hovered = nullptr;
        const ImU32 waitBase = ImGui::GetColorU32(Theme::kControl);
        const ImU32 waitStripe = ImGui::GetColorU32(Theme::FromSrgb(69, 69, 76));
        const ImU32 separator = ImGui::GetColorU32(Theme::kDeepest);
        drawList->PushClipRect(ImVec2(left, contentTop), ImVec2(right, contentBottom), true);
        for (const Zone& zone : zones_) {
            float x0 = toX(zone.start);
            float x1 = toX(zone.end);
            if (x1 < left || x0 > right) {
                continue;
            }
            x0 = std::max(x0, left - 1.0f);
            x1 = std::min(x1, right + 1.0f);
            if (x1 - x0 < 1.0f) {
                x1 = x0 + 1.0f;
            }
            const float y0 = rowTop[zone.row] + rowPadding + static_cast<float>(zone.lane) * laneHeight;
            const float y1 = y0 + laneHeight - 2.0f;
            const SourceInfo& info = sources.Peek(zone.srcloc);
            const bool gpu = rows_[zone.row].kind == RowKind::Gpu;
            const ZoneKind kind = gpu ? ZoneKind::Render : info.kind;
            ImVec4 color = ZoneKindColor(kind);
            if (zone.odd) {
                color = Draw::Darken(color, 0.82f);
            }
            const float drawRight = (x1 - x0 > 3.0f) ? x1 - 1.0f : x1;
            if (zone.merged > 1) {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(drawRight, y1), ImGui::GetColorU32(Theme::WithAlpha(color, 0.45f)));
            } else if (kind == ZoneKind::Wait) {
                Draw::HatchedRect(drawList, ImVec2(x0, y0), ImVec2(drawRight, y1), waitBase, waitStripe, lineHeight * 0.5f);
            } else {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(drawRight, y1), ImGui::GetColorU32(color));
            }
            if (drawRight < x1) {
                drawList->AddRectFilled(ImVec2(drawRight, y0), ImVec2(x1, y1), separator);
            }

            if (zone.merged == 1 && x1 - x0 > lineHeight * 1.5f) {
                const std::string withTime = std::format("{} {} ms", info.name, Text::FormatMs(static_cast<double>(zone.end - zone.start)));
                const char* label = (ImGui::CalcTextSize(withTime.c_str()).x + lineHeight * 0.5f < x1 - x0) ? withTime.c_str() : info.name;
                const ImU32 labelColor = kind == ZoneKind::Wait ? textColor : Draw::TextOn(color);
                drawList->PushClipRect(ImVec2(std::max(x0, left) + 2.0f, y0), ImVec2(std::min(x1, right) - 2.0f, y1), true);
                drawList->AddText(ImVec2(std::max(x0, left) + lineHeight * 0.25f, y0 + (y1 - y0 - lineHeight) * 0.5f), labelColor, label);
                drawList->PopClipRect();
            }
            if (zone.ref.IsValid() && zone.ref == view.selectedZone) {
                drawList->AddRect(ImVec2(x0, y0), ImVec2(drawRight, y1), textColor, 0.0f, 0, 2.0f);
            }
            if (plotHovered && mouse.x >= x0 && mouse.x < x1 && mouse.y >= y0 && mouse.y < y1) {
                hovered = &zone;
            }
        }
        if (hovered) {
            const float x0 = std::max(toX(hovered->start), left);
            const float x1 = std::max(x0 + 1.0f, std::min(toX(hovered->end), right));
            const float y0 = rowTop[hovered->row] + rowPadding + static_cast<float>(hovered->lane) * laneHeight;
            drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y0 + laneHeight - 2.0f), ImGui::GetColorU32(Theme::WithAlpha(Theme::kText, 0.75f)));
        }
        drawList->PopClipRect();

        // 操作を受ける面
        ImGui::SetCursorScreenPos(ImVec2(left, contentTop));
        ImGui::InvisibleButton("##plot", ImVec2(width, std::max(1.0f, contentBottom - contentTop)),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        HandleInput(frames, view, left, width, hovered);
        if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            DrawTooltip(*hovered, sources, frames);
            hoverSrcloc_ = hovered->srcloc;
            hoverGpu_ = rows_[hovered->row].kind == RowKind::Gpu;
            hasHover_ = hovered->merged == 1;
        } else {
            hasHover_ = false;
        }

        ImGui::SetCursorScreenPos(ImVec2(left, contentBottom + lineHeight * 0.6f));
        DrawLegend();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void TimelineView::DrawHeader(const FrameSnapshot& frames, ViewState& view)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (!view.hasSelectedFrame || view.selectedFrame >= frames.Count()) {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted("上のフレームの棒をクリックすると、そのフレームの中身がここに出ます。");
            ImGui::PopStyleColor();
            return;
        }
        const FrameTime& frame = frames.At(view.selectedFrame);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("フレーム %s", Text::FormatCount(frames.Number(view.selectedFrame)).c_str());
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.5f);
        std::string detail = Text::FormatMs(static_cast<double>(frame.Duration())) + " ms";
        if (frame.wait >= 0) {
            detail += std::format("　仕事 {} ms・待ち {} ms", Text::FormatMs(static_cast<double>(frame.Work())),
                Text::FormatMs(static_cast<double>(frame.wait)));
        }
        if (gpuFrameTime_ >= 0) {
            detail += std::format("・GPU {} ms", Text::FormatMs(static_cast<double>(gpuFrameTime_)));
        }
        if (!frames.IsComplete(view.selectedFrame)) {
            detail += "（まだ終わっていない）";
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted(detail.c_str());
        ImGui::PopStyleColor();

        const char* hint = "ホイールで拡大・右ドラッグで移動・ダブルクリックでその区間いっぱいに";
        const char* resetLabel = "フレーム全体";
        const float buttonWidth = ImGui::CalcTextSize(resetLabel).x + style.FramePadding.x * 2.0f;
        const float hintWidth = ImGui::CalcTextSize(hint).x + style.ItemSpacing.x * 2.0f;
        ImGui::SameLine();
        const float cursor = ImGui::GetCursorPosX();
        const bool showHint = right - cursor >= hintWidth + buttonWidth;
        ImGui::SameLine(std::max(cursor, right - buttonWidth - (showHint ? hintWidth : 0.0f)));
        if (showHint) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
            ImGui::TextUnformatted(hint);
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);
        }
        if (ImGui::Button(resetLabel)) {
            view.viewStart = frame.begin;
            view.viewEnd = std::max(frame.end, frame.begin + 1000);
        }
        ImGui::SetItemTooltip("選んだフレームの始まりから終わりまでを出します。");
    }

    void TimelineView::DrawRuler(ImDrawList* drawList, const FrameSnapshot& frames, const ViewState& view, float left, float top,
        float width, float height, float labelWidth)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const int64_t span = std::max<int64_t>(1, view.viewEnd - view.viewStart);
        const double nsPerPixel = static_cast<double>(span) / static_cast<double>(width);
        const int64_t originTime = (view.hasSelectedFrame && view.selectedFrame < frames.Count())
            ? frames.At(view.selectedFrame).begin : view.viewStart;
        const double step = NiceStep(nsPerPixel * lineHeight * 5.0);
        const float bottom = top + height;
        const ImU32 lineColor = ImGui::GetColorU32(Theme::kOutline);
        const ImU32 dim = ImGui::GetColorU32(Theme::kTextDim);

        drawList->AddText(ImVec2(left - labelWidth, top + (height - lineHeight) * 0.3f), ImGui::GetColorU32(Theme::kTextMute), "ms");
        drawList->PushClipRect(ImVec2(left, top), ImVec2(left + width, bottom), true);
        const double startOffset = static_cast<double>(view.viewStart - originTime);
        const double endOffset = static_cast<double>(view.viewEnd - originTime);
        const double halfStep = step * 0.5;
        for (double tick = std::floor(startOffset / halfStep) * halfStep; tick <= endOffset; tick += halfStep) {
            const float x = left + static_cast<float>((tick - startOffset) / nsPerPixel);
            const bool major = std::fmod(std::abs(std::round(tick / halfStep)), 2.0) < 0.5;
            const float tickHeight = major ? height * 0.35f : height * 0.18f;
            drawList->AddLine(ImVec2(x, bottom - tickHeight), ImVec2(x, bottom), lineColor);
            if (major) {
                const std::string label = TickLabel(tick, step);
                const float textWidth = ImGui::CalcTextSize(label.c_str()).x;
                const float textX = std::clamp(x - textWidth * 0.5f, left + 2.0f, std::max(left + 2.0f, left + width - textWidth - 2.0f));
                drawList->AddText(ImVec2(textX, top + 1.0f), dim, label.c_str());
            }
        }
        drawList->PopClipRect();
        drawList->AddLine(ImVec2(left, bottom), ImVec2(left + width, bottom), lineColor);
    }

    void TimelineView::HandleInput(const FrameSnapshot& frames, ViewState& view, float left, float width, const Zone* hovered)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool itemHovered = ImGui::IsItemHovered();
        const int64_t span = std::max<int64_t>(1, view.viewEnd - view.viewStart);
        const double nsPerPixel = static_cast<double>(span) / static_cast<double>(width);

        if (itemHovered && io.MouseWheel != 0.0f) {
            const double mouseTime = static_cast<double>(view.viewStart)
                + static_cast<double>(std::clamp(io.MousePos.x - left, 0.0f, width)) * nsPerPixel;
            const double maxSpan = std::max<double>(1'000'000.0, static_cast<double>(captureEnd_ - captureStart_) * 1.1);
            const double newSpan = std::clamp(static_cast<double>(span) * std::pow(kWheelZoom, io.MouseWheel),
                static_cast<double>(kMinSpan), maxSpan);
            const double newStart = mouseTime - (mouseTime - static_cast<double>(view.viewStart)) * newSpan / static_cast<double>(span);
            view.viewStart = static_cast<int64_t>(newStart);
            view.viewEnd = static_cast<int64_t>(newStart + newSpan);
            view.followLatest = false;
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) && io.MouseDelta.x != 0.0f) {
            const int64_t shift = static_cast<int64_t>(static_cast<double>(io.MouseDelta.x) * nsPerPixel);
            view.viewStart -= shift;
            view.viewEnd -= shift;
            view.followLatest = false;
        }

        if (!hovered) {
            return;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (hovered->ref.IsValid()) {
                view.selectedZone = hovered->ref;
                const size_t frame = frames.FindFrame(hovered->start);
                if (frame != view.selectedFrame || !view.hasSelectedFrame) {
                    view.selectedFrame = frame;
                    view.hasSelectedFrame = true;
                    if (frame < view.stripFirst) {
                        view.stripFirst = frame;
                    } else if (frame >= view.stripFirst + kStripFrames) {
                        view.stripFirst = std::min(frame + 1 - kStripFrames, MaxStripFirst(frames));
                    }
                }
            }
            view.followLatest = false;
        }
        if (itemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const int64_t margin = std::max<int64_t>(kMinSpan / 2, (hovered->end - hovered->start) / 20);
            view.viewStart = hovered->start - margin;
            view.viewEnd = hovered->end + margin;
            view.followLatest = false;
        }
    }

    void TimelineView::DrawTooltip(const Zone& zone, const SourceInfoCache& sources, const FrameSnapshot& frames)
    {
        if (!ImGui::BeginTooltip()) {
            return;
        }
        const float lineHeight = ImGui::GetTextLineHeight();
        ImGui::PushTextWrapPos(lineHeight * 30.0f);
        const int64_t duration = zone.end - zone.start;
        if (zone.merged > 1) {
            ImGui::Text("小さな区間 %s 個", Text::FormatCount(zone.merged).c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::Text("始まりから終わりまで %s ms。拡大すると分けて見えます。", Text::FormatMs(static_cast<double>(duration)).c_str());
            ImGui::PopStyleColor();
        } else {
            const SourceInfo& info = sources.Peek(zone.srcloc);
            const bool gpu = rows_[zone.row].kind == RowKind::Gpu;
            ImGui::TextUnformatted(info.name);
            ImGui::SameLine(0.0f, lineHeight * 0.8f);
            ImGui::Text("%s ms", Text::FormatMs(static_cast<double>(duration)).c_str());
            if (frames.Count() > 0) {
                const int64_t frameDuration = frames.At(frames.FindFrame(zone.start)).Duration();
                if (frameDuration > 0) {
                    ImGui::SameLine(0.0f, lineHeight * 0.8f);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                    ImGui::Text("このフレームの %.1f %%", 100.0 * static_cast<double>(duration) / static_cast<double>(frameDuration));
                    ImGui::PopStyleColor();
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::Text("%s・%s:%u", gpu ? "GPU" : ZoneKindLabel(info.kind), std::string(Text::FileName(info.file)).c_str(), info.line);
            if (hoverStats_.valid && hoverStats_.srcloc == zone.srcloc && hoverStats_.gpu == gpu) {
                ImGui::Text("平均 %s ms（1 回あたり）・最大 %s ms・%s 回",
                    Text::FormatMs(static_cast<double>(hoverStats_.total) / static_cast<double>(hoverStats_.count)).c_str(),
                    Text::FormatMs(static_cast<double>(hoverStats_.max)).c_str(), Text::FormatCount(hoverStats_.count).c_str());
            }
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    void TimelineView::DrawLegend()
    {
        const float gap = ImGui::GetStyle().ItemSpacing.x * 2.5f;
        Draw::LegendItem(ZoneKindColor(ZoneKind::Frame), "フレームの枠");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(ZoneKindColor(ZoneKind::Update), "更新");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(ZoneKindColor(ZoneKind::Script), "スクリプト");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(ZoneKindColor(ZoneKind::Render), "描画（GPU も同じ色）");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(ZoneKindColor(ZoneKind::Wait), "待ち", 0.0f, true);
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(ZoneKindColor(ZoneKind::Load), "読み込み");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(ZoneKindColor(ZoneKind::Other), "ほか");
        ImGui::SameLine(0.0f, gap);
        Draw::LegendItem(Theme::WithAlpha(ZoneKindColor(ZoneKind::Update), 0.45f), "細かい区間をまとめたもの");
    }
}
