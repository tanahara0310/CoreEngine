#include "View/ZoneDetailPanel.h"

#include "Capture/FrameSnapshot.h"
#include "Capture/SourceInfoCache.h"
#include "Capture/TracyAccess.h"
#include "Text/TextFormat.h"
#include "Text/ZoneDescriptions.h"
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

        /// @brief 受け取りの最中に数字を写し直す間隔（秒）
        constexpr double kLiveCopyInterval = 0.5;

        /// @brief 中身の一覧に名前を出す子の数（残りは「ほか」にまとめる）
        constexpr size_t kShownChildren = 8;

        /// @brief 回数・合計・二乗和から 1 回あたりの標準偏差を出す
        double StandardDeviation(uint64_t count, int64_t total, double sumSq)
        {
            if (count < 2) {
                return 0.0;
            }
            const double n = static_cast<double>(count);
            const double mean = static_cast<double>(total) / n;
            const double squares = sumSq - 2.0 * static_cast<double>(total) * mean + mean * mean * n;
            return std::sqrt(std::max(0.0, squares / (n - 1.0)));
        }

        /// @brief 子の時間を場所ごとにまとめる
        void AddChild(std::vector<ZoneDetailPanel::Child>& children, const tracy::Worker& worker, SourceInfoCache& sources,
            int16_t srcloc, int64_t time)
        {
            for (ZoneDetailPanel::Child& child : children) {
                if (child.srcloc == srcloc) {
                    child.time += time;
                    ++child.calls;
                    return;
                }
            }
            children.push_back(ZoneDetailPanel::Child{ srcloc, sources.Get(worker, srcloc).name, time, 1 });
        }

        /// @brief 字の高さの真ん中に色の四角を描き、その分だけ右へ進める
        void Swatch(const ImVec4& color)
        {
            const float size = ImGui::GetTextLineHeight() * 0.6f;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float top = pos.y + (ImGui::GetTextLineHeight() - size) * 0.5f;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, top), ImVec2(pos.x + size, top + size), ImGui::GetColorU32(color));
            ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
        }

        /// @brief 小さな札（灰の地に文字）
        void Chip(const std::string& text)
        {
            const ImVec2 size = ImGui::CalcTextSize(text.c_str());
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float padding = ImGui::GetTextLineHeight() * 0.35f;
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x + padding * 2.0f, pos.y + size.y + 2.0f),
                ImGui::GetColorU32(Theme::kControl));
            ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + padding, pos.y + 1.0f), ImGui::GetColorU32(Theme::kText), text.c_str());
            ImGui::Dummy(ImVec2(size.x + padding * 2.0f, size.y + 2.0f));
        }

        /// @brief 右に寄せた文字
        void TextRight(const std::string& text, const ImVec4& color)
        {
            const float width = ImGui::CalcTextSize(text.c_str()).x;
            const float space = ImGui::GetContentRegionAvail().x - width;
            if (space > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + space);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
        }
    }

    void ZoneDetailPanel::Reset()
    {
        detail_ = Detail{};
        shown_ = ZoneRef{};
        checkedFrame_ = static_cast<size_t>(-1);
        lastCopy_ = -1.0;
    }

    void ZoneDetailPanel::Update(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, ViewState& view,
        bool live, double now)
    {
        Retarget(worker, frames, view);
        const ZoneRef& ref = view.selectedZone;
        if (!ref.IsValid()) {
            detail_.valid = false;
            shown_ = ZoneRef{};
            return;
        }
        const bool changed = !(ref == shown_);
        const bool refresh = live && now - lastCopy_ >= kLiveCopyInterval;
        if (!changed && !refresh && detail_.statsReady) {
            return;
        }
        if (ref.cpu) {
            CollectCpu(worker, frames, sources, ref);
        } else {
            CollectGpu(worker, frames, sources, ref);
        }
        shown_ = ref;
        lastCopy_ = now;
    }

    void ZoneDetailPanel::Retarget(const tracy::Worker& worker, const FrameSnapshot& frames, ViewState& view)
    {
        if (!view.hasSelectedFrame || view.selectedFrame >= frames.Count() || view.selectedFrame == checkedFrame_) {
            return;
        }
        checkedFrame_ = view.selectedFrame;
        ZoneRef& ref = view.selectedZone;
        if (!ref.IsValid()) {
            return;
        }
        const FrameTime& frame = frames.At(view.selectedFrame);
        if (ref.cpu) {
            if (frames.FindFrame(ref.cpu->Start()) == view.selectedFrame) {
                return;
            }
            if (const tracy::ThreadData* thread = worker.GetThreadData(ref.thread)) {
                if (const tracy::ZoneEvent* found = Access::FindCpu(worker, thread->timeline, ref.cpu->SrcLoc(), frame.begin, frame.end, 0)) {
                    ref.cpu = found;
                }
            }
            return;
        }
        if (frames.FindFrame(ref.gpu->GpuStart()) == view.selectedFrame) {
            return;
        }
        const auto& contexts = worker.GetGpuData();
        if (ref.thread < contexts.size()) {
            for (const auto& entry : contexts[ref.thread]->threadData) {
                if (const tracy::GpuEvent* found = Access::FindGpu(worker, entry.second.timeline, ref.gpu->SrcLoc(), frame.begin, frame.end, 0)) {
                    ref.gpu = found;
                    return;
                }
            }
        }
    }

    void ZoneDetailPanel::CollectCpu(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ZoneRef& ref)
    {
        Detail detail;
        const tracy::ZoneEvent& zone = *ref.cpu;
        const int16_t srcloc = zone.SrcLoc();
        const SourceInfo& info = sources.Get(worker, srcloc);
        detail.valid = true;
        detail.name = info.name;
        detail.description = ZoneDescriptions::Instance().Describe(detail.name, info.file);
        detail.location = std::format("{}:{}", Text::FileName(info.file), info.line);
        detail.kind = info.kind;
        const tracy::ThreadData* mainThread = frames.MainThread();
        detail.place = (mainThread && mainThread->id == ref.thread) ? "メインスレッド" : "ワーカー";
        detail.duration = worker.GetZoneEnd(zone) - zone.Start();
        detail.frame = frames.FindFrame(zone.Start());
        const FrameTime& frame = frames.At(detail.frame);
        detail.frameDuration = frame.Duration();

        if (worker.AreSourceLocationZonesReady()) {
            const auto& map = worker.GetSourceLocationZones();
            const auto it = map.find(srcloc);
            if (it != map.end()) {
                const auto& zones = it->second;
                detail.count = zones.zones.size();
                detail.total = zones.total;
                detail.max = zones.max;
                detail.stddev = StandardDeviation(detail.count, zones.total, zones.sumSq);
                int64_t longest = -1;
                const tracy::ZoneEvent* longestEvent = nullptr;
                for (const auto& entry : zones.zones) {
                    const tracy::ZoneEvent* event = entry.Zone();
                    if (!event->IsEndValid()) {
                        continue;
                    }
                    const int64_t start = event->Start();
                    const int64_t time = event->End() - start;
                    const uint64_t thread = worker.DecompressThread(entry.Thread());
                    if (time > longest) {
                        longest = time;
                        longestEvent = event;
                        detail.maxRef = ZoneRef{ event, nullptr, thread };
                    }
                    if (start >= frame.begin && start < frame.end && thread == ref.thread) {
                        detail.frameTotal += time;
                        ++detail.frameCalls;
                    }
                }
                if (longestEvent) {
                    detail.maxFrame = frames.FindFrame(longestEvent->Start());
                    const tracy::ZoneEvent* current = longestEvent;
                    for (int depth = 0; depth < Access::kMaxSearchDepth && current->HasChildren(); ++depth) {
                        const auto& children = worker.GetZoneChildren(current->Child());
                        const tracy::ZoneEvent* heaviest = nullptr;
                        int64_t heaviestTime = 0;
                        for (size_t i = 0; i < children.size(); ++i) {
                            const tracy::ZoneEvent& child = Access::At(children, i);
                            const int64_t time = worker.GetZoneEnd(child) - child.Start();
                            if (time > heaviestTime) {
                                heaviestTime = time;
                                heaviest = &child;
                            }
                        }
                        if (!heaviest || heaviestTime * 2 < worker.GetZoneEnd(*current) - current->Start()) {
                            break;
                        }
                        current = heaviest;
                        detail.maxChildName = sources.Get(worker, heaviest->SrcLoc()).name;
                        detail.maxChildTime = heaviestTime;
                    }
                }
                detail.statsReady = true;
            }
        }
        if (detail.frameCalls == 0) {
            detail.frameTotal = detail.duration;
            detail.frameCalls = 1;
        }

        int64_t childTotal = 0;
        if (zone.HasChildren()) {
            const auto& children = worker.GetZoneChildren(zone.Child());
            for (size_t i = 0; i < children.size(); ++i) {
                const tracy::ZoneEvent& child = Access::At(children, i);
                const int64_t time = worker.GetZoneEnd(child) - child.Start();
                AddChild(detail.children, worker, sources, child.SrcLoc(), time);
                childTotal += time;
            }
        }
        detail.self = std::max<int64_t>(0, detail.duration - childTotal);
        std::stable_sort(detail.children.begin(), detail.children.end(), [](const Child& a, const Child& b) { return a.time > b.time; });
        detail_ = std::move(detail);
    }

    void ZoneDetailPanel::CollectGpu(const tracy::Worker& worker, const FrameSnapshot& frames, SourceInfoCache& sources, const ZoneRef& ref)
    {
        Detail detail;
        const tracy::GpuEvent& zone = *ref.gpu;
        const int16_t srcloc = zone.SrcLoc();
        const SourceInfo& info = sources.Get(worker, srcloc);
        detail.valid = true;
        detail.gpu = true;
        detail.name = info.name;
        detail.description = ZoneDescriptions::Instance().Describe(detail.name, info.file);
        detail.location = std::format("{}:{}", Text::FileName(info.file), info.line);
        detail.kind = ZoneKind::Render;
        const auto& contexts = worker.GetGpuData();
        detail.place = ref.thread < contexts.size() ? "GPU・" + Access::QueueLabel(worker, *contexts[ref.thread]) : std::string("GPU");
        detail.duration = worker.GetZoneEnd(zone) - zone.GpuStart();
        detail.frame = frames.FindFrame(zone.GpuStart());
        const FrameTime& frame = frames.At(detail.frame);
        detail.frameDuration = frame.Duration();

        if (worker.AreGpuSourceLocationZonesReady()) {
            const auto& map = worker.GetGpuSourceLocationZones();
            const auto it = map.find(srcloc);
            if (it != map.end()) {
                const auto& zones = it->second;
                detail.count = zones.zones.size();
                detail.total = zones.total;
                detail.max = zones.max;
                detail.stddev = StandardDeviation(detail.count, zones.total, zones.sumSq);
                int64_t longest = -1;
                const tracy::GpuEvent* longestEvent = nullptr;
                for (const auto& entry : zones.zones) {
                    const tracy::GpuEvent* event = entry.Zone();
                    if (event->GpuEnd() < 0 || event->GpuStart() < 0) {
                        continue;
                    }
                    const int64_t start = event->GpuStart();
                    const int64_t time = event->GpuEnd() - start;
                    if (time > longest) {
                        longest = time;
                        longestEvent = event;
                        detail.maxRef = ZoneRef{ nullptr, event, ref.thread };
                    }
                    if (start >= frame.begin && start < frame.end) {
                        detail.frameTotal += time;
                        ++detail.frameCalls;
                    }
                }
                if (longestEvent) {
                    detail.maxFrame = frames.FindFrame(longestEvent->GpuStart());
                }
                detail.statsReady = true;
            }
        }
        if (detail.frameCalls == 0) {
            detail.frameTotal = detail.duration;
            detail.frameCalls = 1;
        }

        int64_t childTotal = 0;
        if (zone.Child() >= 0) {
            const auto& children = worker.GetGpuChildren(zone.Child());
            for (size_t i = 0; i < children.size(); ++i) {
                const tracy::GpuEvent& child = Access::At(children, i);
                if (child.GpuStart() < 0) {
                    continue;
                }
                const int64_t time = worker.GetZoneEnd(child) - child.GpuStart();
                AddChild(detail.children, worker, sources, child.SrcLoc(), time);
                childTotal += time;
            }
        }
        detail.self = std::max<int64_t>(0, detail.duration - childTotal);
        std::stable_sort(detail.children.begin(), detail.children.end(), [](const Child& a, const Child& b) { return a.time > b.time; });
        detail_ = std::move(detail);
    }

    void ZoneDetailPanel::Draw(const FrameSnapshot& frames, ViewState& view)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted("選んだ区間");
        ImGui::PopStyleColor();
        if (!detail_.valid || !view.selectedZone.IsValid()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted("タイムラインの区間をクリックすると、ここに詳しく出ます。");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            return;
        }

        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(detail_.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        if (!detail_.description.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(detail_.description.c_str());
            ImGui::PopTextWrapPos();
        }
        Chip(detail_.place);
        ImGui::SameLine(0.0f, lineHeight * 0.6f);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted(detail_.location.c_str());
        ImGui::Text("種類：%s", detail_.gpu ? "GPU の描画" : ZoneKindLabel(detail_.kind));
        ImGui::PopStyleColor();
        ImGui::Spacing();

        DrawNumbers(frames, view);

        // いちばん長かった 1 回が目標を超えていれば、そのフレームと中の手がかり
        if (detail_.statsReady && detail_.maxRef.IsValid() && FrameLevelOf(detail_.max, view.TargetNs()) != FrameLevel::Normal) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::FromSrgb(58, 38, 34));
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::FromSrgb(107, 58, 51));
            ImGui::BeginChild("##maxNote", ImVec2(0.0f, 0.0f),
                ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PushTextWrapPos(0.0f);
            std::string note = std::format("最大はフレーム {}（{} ms）。", Text::FormatCount(frames.Number(detail_.maxFrame)),
                Text::FormatMs(static_cast<double>(detail_.max)));
            if (!detail_.maxChildName.empty()) {
                note += std::format("その中では {} が {} ms かかっています。", detail_.maxChildName,
                    Text::FormatMs(static_cast<double>(detail_.maxChildTime)));
            }
            ImGui::TextUnformatted(note.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("そのフレームへ")) {
                SelectFrame(view, frames, detail_.maxFrame);
                view.selectedZone = detail_.maxRef;
                view.followLatest = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextUnformatted("中身（選んだ 1 回）");
        DrawChildren();

        ImGui::Spacing();
        if (ImGui::Button("統計で見る")) {
            view.statsFilter = detail_.name;
            view.statsFilterGpu = detail_.gpu;
            view.statsFilterRequested = true;
            view.tab = ViewTab::Stats;
            view.tabRequested = true;
        }
        ImGui::SetItemTooltip("区間の統計の表を、この区間の名前で絞り込んで開きます。");
    }

    void ZoneDetailPanel::DrawNumbers(const FrameSnapshot& frames, const ViewState& view)
    {
        const ImGuiTableFlags flags = ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame;
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, Theme::kBorder);
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, Theme::kBorder);
        if (!ImGui::BeginTable("##numbers", 2, flags)) {
            ImGui::PopStyleColor(2);
            return;
        }
        const float bigSize = ImGui::GetStyle().FontSizeBase * 1.15f;
        const auto cell = [&](const char* label, const std::string& value, const ImVec4& color, bool big, const std::string& note = {}) {
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            if (big) {
                ImGui::PushFont(nullptr, bigSize);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopStyleColor();
            if (big) {
                ImGui::PopFont();
            }
            if (!note.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::TextUnformatted(note.c_str());
                ImGui::PopStyleColor();
            }
        };

        const double frameCount = frames.Count() > 0 ? static_cast<double>(frames.Count()) : 1.0;
        cell("このフレーム", Text::FormatMs(static_cast<double>(detail_.frameTotal)) + " ms", Theme::kText, true,
            detail_.frameCalls > 1 ? std::format("{} 回の合計", detail_.frameCalls) : std::string());
        if (detail_.statsReady) {
            const FrameLevel level = FrameLevelOf(detail_.max, view.TargetNs());
            const ImVec4 maxColor = level == FrameLevel::Far ? Theme::kError : (level == FrameLevel::Over ? Theme::kWarm : Theme::kText);
            cell("平均（1 フレームあたり）", Text::FormatMs(static_cast<double>(detail_.total) / frameCount) + " ms", Theme::kText, true);
            cell("最大（1 回）", Text::FormatMs(static_cast<double>(detail_.max)) + " ms", maxColor, true);
            cell("ばらつき（標準偏差）", Text::FormatMs(detail_.stddev) + " ms", Theme::kText, true);
            cell("回数", Text::FormatCount(detail_.count) + " 回", Theme::kText, false,
                std::format("1 フレームに {:.1f} 回", static_cast<double>(detail_.count) / frameCount));
        } else {
            cell("平均（1 フレームあたり）", "集計しています…", Theme::kTextDim, false);
            cell("最大（1 回）", "—", Theme::kTextDim, false);
            cell("ばらつき（標準偏差）", "—", Theme::kTextDim, false);
            cell("回数", "—", Theme::kTextDim, false);
        }
        const double share = detail_.frameDuration > 0
            ? 100.0 * static_cast<double>(detail_.frameTotal) / static_cast<double>(detail_.frameDuration) : 0.0;
        cell("フレームに占める割合", std::format("{:.1f} %", share), Theme::kText, false);
        ImGui::EndTable();
        ImGui::PopStyleColor(2);
    }

    void ZoneDetailPanel::DrawChildren()
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        if (detail_.children.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextMute);
            ImGui::TextUnformatted("子の区間はありません。");
            ImGui::PopStyleColor();
            return;
        }

        // 行：名前の付いた子・ほか・自分だけ
        struct Item
        {
            std::string name;
            int64_t time;
            uint32_t calls;
            ImVec4 color;
        };
        std::vector<Item> items;
        int64_t otherTime = 0;
        size_t otherCount = 0;
        for (size_t i = 0; i < detail_.children.size(); ++i) {
            const Child& child = detail_.children[i];
            if (i < kShownChildren) {
                items.push_back(Item{ child.name, child.time, child.calls, Theme::kSeries[i] });
            } else {
                otherTime += child.time;
                ++otherCount;
            }
        }
        if (otherCount > 0) {
            items.push_back(Item{ std::format("ほか {} 種類", otherCount), otherTime, 0, Theme::kTextMute });
        }
        if (detail_.self > 0) {
            items.push_back(Item{ "自分だけ（子に入らない時間）", detail_.self, 0, Theme::kActive });
        }

        // 積み上げの帯
        const double whole = std::max<double>(1.0, static_cast<double>(detail_.duration));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = lineHeight * 0.75f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float x = pos.x;
        for (const Item& item : items) {
            const float w = static_cast<float>(static_cast<double>(item.time) / whole) * width;
            drawList->AddRectFilled(ImVec2(x, pos.y), ImVec2(std::min(pos.x + width, x + w), pos.y + height), ImGui::GetColorU32(item.color));
            x += w;
        }
        drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(Theme::kBorder));
        ImGui::Dummy(ImVec2(width, height));

        if (!ImGui::BeginTable("##children", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            return;
        }
        ImGui::TableSetupColumn("##swatch", ImGuiTableColumnFlags_WidthFixed, lineHeight * 0.6f);
        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##ms", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.000 ms").x);
        ImGui::TableSetupColumn("##share", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("100 %").x);
        for (const Item& item : items) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            Swatch(item.color);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.name.c_str());
            if (item.calls > 1) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
                ImGui::Text("×%u", item.calls);
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            TextRight(Text::FormatMs(static_cast<double>(item.time)) + " ms", Theme::kText);
            ImGui::TableNextColumn();
            TextRight(std::format("{:.0f} %", 100.0 * static_cast<double>(item.time) / whole), Theme::kTextDim);
        }
        ImGui::EndTable();
    }
}
