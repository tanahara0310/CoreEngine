#include "pch.h"
#include "Editor/ImGui/ProfilerPanel.h"

#ifdef CORE_EDITOR

#include "Diagnostics/EngineStats.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/ThreadProfilerUI.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/ImGui/Widgets/PassTimingTable.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Script/ScriptComponentType.h"
#include "Script/ScriptHost.h"
#include "Script/ScriptSubsystem.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 1 フレームの予算（60FPS）
        constexpr float kBudgetMs = 1000.0f / 60.0f;

        /// パス別の上位に出す数
        constexpr size_t kTopPassCount = 5;

        /// 履歴の CSV の置き場（作業フォルダからの相対）
        constexpr const char* kExportDirectory = "Captures/Profiling";

        /// @brief スクリプトの実行環境（無ければ nullptr）
        const ScriptHost* FindScriptHost(EngineSystem* engine)
        {
            ScriptSubsystem* const script = engine ? engine->GetSubsystem<ScriptSubsystem>() : nullptr;
            return script ? script->GetHost() : nullptr;
        }

        /// @brief ラベルと値の 1 行（値は右の列にそろえる）
        void ValueRow(const char* label, const ImVec4& labelColor, const std::string& value, const ImVec4& valueColor)
        {
            ImGui::TextColored(labelColor, "%s", label);
            ImGui::SameLine((std::max)(170.0f, ImGui::CalcTextSize(label).x + 16.0f));
            ImGui::TextColored(valueColor, "%s", value.c_str());
        }

        /// @brief 系列の色の見本（小さな丸）と名前
        void LegendItem(const char* label, const ImVec4& color)
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float y = cursor.y + ImGui::GetTextLineHeight() * 0.5f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cursor.x + 4.0f, y), 3.5f, ImGui::GetColorU32(color));
            ImGui::Dummy(ImVec2(10.0f, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(Theme::kTextDim, "%s", label);
        }
    }

    void ProfilerPanel::Initialize(EngineSystem* engine, GpuTimestampProfiler* gpuProfiler, ThreadProfilerUI* threadProfiler)
    {
        engine_ = engine;
        gpuProfiler_ = gpuProfiler;
        threadProfiler_ = threadProfiler;
    }

    void ProfilerPanel::Collect()
    {
        if (!recording_) {
            return;
        }

        FrameSample sample;
        sample.frameMs = Time::UnscaledDeltaTime() * 1000.0f;
        sample.updateMs = EngineStats::GetInstance().GetFrameTimings().updateMs;
        if (const ScriptHost* const host = FindScriptHost(engine_)) {
            sample.scriptMs = static_cast<float>(host->GetFrameStats().updateMs);
        }
        if (gpuProfiler_ && gpuProfiler_->IsInitialized()) {
            const auto& total = gpuProfiler_->GetResults()[static_cast<uint32_t>(GpuTimestampSlot::Total)];
            sample.renderMs = total.cpuMs;
            sample.gpuMs = total.gpuMs;
        }

        history_[static_cast<size_t>(historyHead_)] = sample;
        historyHead_ = (historyHead_ + 1) % kHistorySize;
        historyCount_ = (std::min)(historyCount_ + 1, kHistorySize);
    }

    const ProfilerPanel::FrameSample& ProfilerPanel::SampleAt(int i) const
    {
        const int oldest = (historyHead_ - historyCount_ + kHistorySize) % kHistorySize;
        return history_[static_cast<size_t>((oldest + i) % kHistorySize)];
    }

    void ProfilerPanel::Draw()
    {
        DrawHeader();
        ImGui::Spacing();

        // 左：内訳のタブ（伸びる）／右：スクリプト（固定幅）
        constexpr float kRightWidth = 300.0f;
        const float leftWidth = (std::max)(200.0f, ImGui::GetContentRegionAvail().x - kRightWidth - 8.0f);

        if (auto left = UI::Scope::ChildScope("##profilerLeft", ImVec2(leftWidth, 0.0f))) {
            if (auto tabs = UI::Scope::TabBarScope("##profilerTabs")) {
                // 初めて描くときはフレーム内訳を開いておく
                const ImGuiTabItemFlags firstFlags = firstDraw_ ? ImGuiTabItemFlags_SetSelected : 0;
                firstDraw_ = false;
                if (auto tab = UI::Scope::TabItemScope("フレーム内訳", nullptr, firstFlags)) {
                    DrawGraph();
                    ImGui::Spacing();
                    DrawFrameBreakdown();
                }
                if (auto tab = UI::Scope::TabItemScope("パス別")) {
                    if (gpuProfiler_ && gpuProfiler_->IsInitialized()) {
                        UI::PassTimingTable("##profilerPasses", gpuProfiler_->GetResults(), true);
                    } else {
                        UI::Hint("GPU の計測が初期化されていません");
                    }
                }
                if (auto tab = UI::Scope::TabItemScope("スレッド")) {
                    if (threadProfiler_) {
                        threadProfiler_->Draw();
                    } else {
                        UI::Hint("スレッドプールの計測がありません");
                    }
                }
            }
        }

        ImGui::SameLine(0.0f, 8.0f);
        if (auto right = UI::Scope::ChildScope("##profilerRight", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
            DrawScriptColumn();
        }
    }

    void ProfilerPanel::DrawHeader()
    {
        if (UI::Bar::Button(recording_ ? "● 記録中" : "○ 止めています", recording_,
            recording_ ? "押すと記録を止め、今のグラフを残す" : "押すと記録を再開する")) {
            recording_ = !recording_;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::kTextMute, "%d / %d フレーム", historyCount_, kHistorySize);

        UI::Bar::Separator();
        if (UI::Bar::Button("CPU", showCpu_)) { showCpu_ = !showCpu_; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("GPU", showGpu_)) { showGpu_ = !showGpu_; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("スクリプト", showScript_)) { showScript_ = !showScript_; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("メモリ", showMemory_)) { showMemory_ = !showMemory_; }

        UI::Bar::Separator();
        if (UI::Bar::Button("CSV 書き出し", false, "控えている履歴を Captures/Profiling へ書き出す")) {
            std::string path;
            if (ExportCsv(path)) {
                lastExportPath_ = path;
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                    "プロファイラの履歴を書き出しました: {}", path);
            }
        }
        if (!lastExportPath_.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Theme::kTextMute, "%s", lastExportPath_.c_str());
        }
    }

    void ProfilerPanel::DrawGraph()
    {
        const ImVec2 size(ImGui::GetContentRegionAvail().x, 110.0f);
        if (size.x <= 1.0f) {
            return;
        }
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        ImGui::InvisibleButton("##frameGraph", size);
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList* const draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(min, max, ImGui::GetColorU32(Theme::kField), 4.0f);

        // 縦軸の上限（予算の 2 倍か、控えの最大の 1.1 倍）
        float top = kBudgetMs * 2.0f;
        for (int i = 0; i < historyCount_; ++i) {
            top = (std::max)(top, SampleAt(i).frameMs * 1.1f);
        }
        const auto yOf = [&](float ms) { return max.y - 2.0f - (ms / top) * (size.y - 4.0f); };
        const auto xOf = [&](int slot) { return min.x + (static_cast<float>(slot) / (kHistorySize - 1)) * size.x; };
        const int firstSlot = kHistorySize - historyCount_;

        // 予算の線
        const float budgetY = yOf(kBudgetMs);
        draw->AddLine(ImVec2(min.x, budgetY), ImVec2(max.x, budgetY),
            ImGui::GetColorU32(Theme::WithAlpha(Theme::kTextMute, 0.7f)));
        draw->AddText(ImVec2(min.x + 6.0f, budgetY - ImGui::GetTextLineHeight() - 1.0f),
            ImGui::GetColorU32(Theme::kTextMute), "16.7 ms（60 FPS）");

        // 系列
        std::vector<ImVec2> points;
        points.reserve(static_cast<size_t>(historyCount_));
        const auto series = [&](const auto& valueOf, const ImVec4& color, float thickness) {
            points.clear();
            for (int i = 0; i < historyCount_; ++i) {
                points.emplace_back(xOf(firstSlot + i), yOf(valueOf(SampleAt(i))));
            }
            if (points.size() >= 2) {
                draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                    ImGui::GetColorU32(color), ImDrawFlags_None, thickness);
            }
            };

        series([](const FrameSample& s) { return s.frameMs; }, Theme::kText, 1.5f);
        if (showCpu_) {
            series([](const FrameSample& s) { return s.updateMs + s.renderMs; }, Theme::kWarn, 1.2f);
        }
        if (showGpu_) {
            series([](const FrameSample& s) { return s.gpuMs; }, Theme::kAccentHover, 1.2f);
        }
        if (showScript_) {
            series([](const FrameSample& s) { return s.scriptMs; }, Theme::kScript, 1.2f);
        }

        // カーソルの位置のフレームの値
        if (hovered && historyCount_ > 0) {
            const float t = (ImGui::GetMousePos().x - min.x) / size.x;
            const int slot = std::clamp(static_cast<int>(t * (kHistorySize - 1) + 0.5f), firstSlot, kHistorySize - 1);
            const FrameSample& sample = SampleAt(slot - firstSlot);
            draw->AddLine(ImVec2(xOf(slot), min.y), ImVec2(xOf(slot), max.y), ImGui::GetColorU32(Theme::kOutline));

            auto tooltip = UI::Scope::TooltipScope();
            if (tooltip) {
                ImGui::TextColored(Theme::kText, "フレーム     %.2f ms", sample.frameMs);
                ImGui::TextColored(Theme::kWarn, "CPU 更新     %.2f ms", sample.updateMs);
                ImGui::TextColored(Theme::kWarn, "CPU 描画     %.2f ms", sample.renderMs);
                ImGui::TextColored(Theme::kAccentHover, "GPU          %.2f ms", sample.gpuMs);
                ImGui::TextColored(Theme::kScript, "スクリプト   %.2f ms", sample.scriptMs);
            }
        }

        // 凡例
        LegendItem("フレーム", Theme::kText);
        ImGui::SameLine(0.0f, 14.0f);
        LegendItem("CPU（更新＋描画の記録）", Theme::kWarn);
        ImGui::SameLine(0.0f, 14.0f);
        LegendItem("GPU", Theme::kAccentHover);
        ImGui::SameLine(0.0f, 14.0f);
        LegendItem("スクリプト", Theme::kScript);
    }

    void ProfilerPanel::DrawFrameBreakdown()
    {
        if (historyCount_ == 0) {
            UI::Hint("まだ記録していません");
            return;
        }
        const FrameSample& latest = SampleAt(historyCount_ - 1);

        UI::SectionHeader("フレーム合計");
        ValueRow("フレーム", Theme::kText, std::format("{:.2f} ms", latest.frameMs),
            latest.frameMs > kBudgetMs * 1.1f ? Theme::kWarn : Theme::kText);

        if (showCpu_) {
            ValueRow("CPU: ゲームの更新", Theme::kTextDim, std::format("{:.2f} ms", latest.updateMs),
                UI::CpuTimeColor(latest.updateMs));
            ValueRow("CPU: 描画の記録", Theme::kTextDim, std::format("{:.2f} ms", latest.renderMs),
                UI::CpuTimeColor(latest.renderMs));
        }
        if (showScript_) {
            std::string scriptText = std::format("{:.2f} ms", latest.scriptMs);
            if (const ScriptHost* const host = FindScriptHost(engine_)) {
                scriptText += std::format("（{} 型 / {} 実体）",
                    host->GetTypes().size(), host->GetFrameStats().liveComponents);
            }
            ValueRow("CPU: スクリプト", Theme::kTextDim, scriptText, Theme::kScript);
        }
        if (showGpu_) {
            ValueRow("GPU: 合計", Theme::kTextDim, std::format("{:.2f} ms", latest.gpuMs),
                UI::GpuTimeColor(latest.gpuMs));
        }

        if (showGpu_ && gpuProfiler_ && gpuProfiler_->IsInitialized()) {
            // 時間の長いパス（フレーム合計とエディタの描画を除く）
            std::vector<const GpuTimingResult*> passes;
            for (const GpuTimingResult& slot : gpuProfiler_->GetResults()) {
                if (slot.gpuMs <= 0.0f || slot.category == GpuTimingCategory::Frame
                    || slot.category == GpuTimingCategory::Editor) {
                    continue;
                }
                passes.push_back(&slot);
            }
            std::sort(passes.begin(), passes.end(),
                [](const GpuTimingResult* a, const GpuTimingResult* b) { return a->gpuMs > b->gpuMs; });

            ImGui::Spacing();
            UI::SectionHeader("GPU パス別（上位）");
            for (size_t i = 0; i < passes.size() && i < kTopPassCount; ++i) {
                ValueRow(passes[i]->name, Theme::kTextDim, std::format("{:.2f} ms", passes[i]->gpuMs),
                    UI::GpuTimeColor(passes[i]->gpuMs));
            }
            if (passes.empty()) {
                UI::Hint("計測されたパスがありません");
            }
        }

        if (showMemory_) {
            const MemoryStats& memory = EngineStats::GetInstance().GetMemoryStats();
            constexpr double kMiB = 1024.0 * 1024.0;
            ImGui::Spacing();
            UI::SectionHeader("メモリ");
            ValueRow("CPU: ワーキングセット", Theme::kTextDim,
                std::format("{:.1f} MB", static_cast<double>(memory.cpuWorkingSetBytes) / kMiB), Theme::kText);
            ValueRow("CPU: プライベート", Theme::kTextDim,
                std::format("{:.1f} MB", static_cast<double>(memory.cpuPrivateBytes) / kMiB), Theme::kText);
            ValueRow("GPU: 使用 / 予算", Theme::kTextDim,
                std::format("{:.1f} / {:.1f} MB",
                    static_cast<double>(memory.gpuMemoryUsageBytes) / kMiB,
                    static_cast<double>(memory.gpuMemoryBudgetBytes) / kMiB),
                Theme::kText);
        }
    }

    void ProfilerPanel::DrawScriptColumn()
    {
        ImGui::TextColored(Theme::kScript, "スクリプト");
        ImGui::Separator();

        const ScriptHost* const host = FindScriptHost(engine_);
        if (!host) {
            UI::Hint("スクリプトの実行環境がありません");
            return;
        }

        UI::SectionHeader("型別の Update コスト");
        std::vector<const ScriptComponentType*> types;
        for (const std::unique_ptr<ScriptComponentType>& type : host->GetTypes()) {
            types.push_back(type.get());
        }
        std::sort(types.begin(), types.end(), [](const ScriptComponentType* a, const ScriptComponentType* b) {
            return a->GetLastFrameCostMs() > b->GetLastFrameCostMs();
            });

        if (types.empty()) {
            UI::Hint("コンポーネントの型がありません");
        }
        for (const ScriptComponentType* const type : types) {
            ImGui::TextColored(Theme::kText, "%s", type->GetDisplayName().c_str());
            ImGui::SameLine(160.0f);
            ImGui::TextColored(Theme::kScript, "%.3f ms", type->GetLastFrameCostMs());
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(Theme::kTextMute, "×%u", type->GetLastFrameInstances());
        }

        ImGui::Spacing();
        UI::SectionHeader("VM");
        const ScriptHost::FrameStats& stats = host->GetFrameStats();
        ValueRow("生きているコンポーネント", Theme::kTextDim, std::to_string(stats.liveComponents), Theme::kText);
        ValueRow("待機中のコンテキスト", Theme::kTextDim, std::to_string(stats.pooledContexts), Theme::kText);
        ValueRow("GC のオブジェクト", Theme::kTextDim, std::to_string(stats.gcObjects), Theme::kText);
    }

    bool ProfilerPanel::ExportCsv(std::string& outPath) const
    {
        std::error_code error;
        const std::filesystem::path directory(kExportDirectory);
        std::filesystem::create_directories(directory, error);

        // 書き出した時刻をファイル名にする
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        const std::string stamp = std::format("{:04}{:02}{:02}_{:02}{:02}{:02}",
            local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
        const std::filesystem::path path = directory / ("Profiler_" + stamp + ".csv");

        std::ofstream file(path, std::ios::binary);
        if (!file) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "プロファイラの履歴を書き出せませんでした: {}", Logger::GetInstance().PathToUtf8(path));
            return false;
        }

        file << "frame,frame_ms,update_cpu_ms,render_cpu_ms,script_ms,gpu_ms\n";
        for (int i = 0; i < historyCount_; ++i) {
            const FrameSample& s = SampleAt(i);
            file << std::format("{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}\n",
                i, s.frameMs, s.updateMs, s.renderMs, s.scriptMs, s.gpuMs);
        }

        outPath = Logger::GetInstance().PathToUtf8(path);
        return true;
    }
}

#endif // CORE_EDITOR
