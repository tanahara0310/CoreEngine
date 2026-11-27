#include "pch.h"
#include "Editor/ImGui/ProfilerPanel.h"

#ifdef CORE_EDITOR

#include "Diagnostics/EngineStats.h"
#include "Editor/External/ProfilerLauncher.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/ThreadProfilerUI.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/ImGui/Widgets/PassTimingTable.h"
#include "Editor/ImGui/Widgets/TimingPlots.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Script/ScriptComponentType.h"
#include "Script/ScriptHost.h"
#include "Script/ScriptSubsystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Profiler/ProfileZone.h"

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

        /// 目標の FPS が取れないときの FPS
        constexpr float kFallbackTargetFps = 60.0f;

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

        /// @brief 目標の FPS（取れなければ 60）
        float TargetFps(EngineSystem* engine)
        {
            const FrameRateController* const frameRate = engine ? engine->GetService<FrameRateController>() : nullptr;
            const float fps = frameRate ? frameRate->GetTargetFPS() : 0.0f;
            return fps > 0.0f ? fps : kFallbackTargetFps;
        }

        /// @brief ラベルと値の 1 行（値は右の列にそろえる）
        void ValueRow(const char* label, const ImVec4& labelColor, const std::string& value, const ImVec4& valueColor)
        {
            ImGui::TextColored(labelColor, "%s", label);
            ImGui::SameLine((std::max)(170.0f, ImGui::CalcTextSize(label).x + 16.0f));
            ImGui::TextColored(valueColor, "%s", value.c_str());
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

        history_.Push(sample);
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
        ImGui::TextColored(Theme::kTextMute, "%d / %d フレーム", history_.Count(), kHistorySize);

        UI::Bar::Separator();
        if (UI::Bar::Button("CPU", showCpu_)) { showCpu_ = !showCpu_; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("GPU", showGpu_)) { showGpu_ = !showGpu_; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("スクリプト", showScript_)) { showScript_ = !showScript_; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UI::Bar::Button("メモリ", showMemory_)) { showMemory_ = !showMemory_; }

#if CORE_TRACY_ENABLED
        UI::Bar::Separator();
        if (UI::Bar::Button("詳しく見る", false, "CoreEngine Profiler を開いて、このエディタへつなぐ（Ctrl+7）")) {
            Editor::OpenProfiler();
        }
#endif

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
        // 値は 1 行ずつ古い順に並べる（フレーム・GPU・CPU・ゲームの更新・描画の記録・スクリプト）
        constexpr size_t kRowCount = 6;
        const int count = history_.Count();
        const size_t stride = static_cast<size_t>(count);
        std::vector<float> values(stride * kRowCount);
        const auto row = [&](size_t index) { return values.data() + stride * index; };
        for (int i = 0; i < count; ++i) {
            const FrameSample& sample = history_.At(i);
            const size_t f = static_cast<size_t>(i);
            row(0)[f] = sample.frameMs;
            row(1)[f] = sample.gpuMs;
            row(2)[f] = sample.updateMs + sample.renderMs;
            row(3)[f] = sample.updateMs;
            row(4)[f] = sample.renderMs;
            row(5)[f] = sample.scriptMs;
        }

        std::vector<UI::TimeLine> lines;
        lines.push_back({ "フレーム", row(0), Theme::kSeries[0] });
        if (showGpu_) {
            lines.push_back({ "GPU", row(1), Theme::kSeries[1] });
        }
        if (showCpu_) {
            lines.push_back({ "CPU（更新＋描画の記録）", row(2), Theme::kSeries[2] });
            lines.push_back({ "ゲームの更新", row(3), {}, true });
            lines.push_back({ "描画の記録", row(4), {}, true });
        }
        if (showScript_) {
            lines.push_back({ "スクリプト", row(5), Theme::kScript });
        }
        UI::DrawTimeLinesPlot("##frameGraph", lines, count, kHistorySize, TargetFps(engine_), 140.0f);
    }

    void ProfilerPanel::DrawFrameBreakdown()
    {
        if (history_.Count() == 0) {
            UI::Hint("まだ記録していません");
            return;
        }
        const FrameSample& latest = history_.At(history_.Count() - 1);

        UI::SectionHeader("フレーム合計");
        ValueRow("フレーム", Theme::kText, std::format("{:.2f} ms", latest.frameMs),
            latest.frameMs > 1000.0f / TargetFps(engine_) * 1.1f ? Theme::kWarn : Theme::kText);

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
        for (int i = 0; i < history_.Count(); ++i) {
            const FrameSample& s = history_.At(i);
            file << std::format("{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}\n",
                i, s.frameMs, s.updateMs, s.renderMs, s.scriptMs, s.gpuMs);
        }

        outPath = Logger::GetInstance().PathToUtf8(path);
        return true;
    }
}

#endif // CORE_EDITOR
