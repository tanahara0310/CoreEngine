#include "pch.h"
#include "CollisionTestReport.h"

#include "Utility/Logger/Logger.h"

#ifdef USE_IMGUI
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CollisionTest
{
    namespace {
        /// @brief 判定状態の表示ラベル
        const char* ToLabel(Status status)
        {
            switch (status) {
            case Status::Pass: return "PASS";
            case Status::Fail: return "FAIL";
            default:           return "....";
            }
        }
    }

    Report& Report::Get()
    {
        static Report instance;
        return instance;
    }

    void Report::BeginSession(const std::string& sessionName)
    {
        sessionName_ = sessionName;
        cases_.clear();
        log_.clear();
        frame_ = 0;
        restartRequested_ = false;
    }

    void Report::Upsert(const CaseResult& result)
    {
        for (auto& existing : cases_) {
            if (existing.id != result.id) { continue; }

            // Pending → 確定 の遷移だけログに残す（毎フレーム呼ばれるため）
            const bool decidedNow =
                (existing.status == Status::Pending && result.status != Status::Pending);
            existing = result;
            if (decidedNow) { LogVerdict(existing); }
            return;
        }

        cases_.push_back(result);
        if (result.status != Status::Pending) { LogVerdict(result); }
    }

    void Report::LogVerdict(const CaseResult& result)
    {
        std::string line = std::string(ToLabel(result.status)) + " " + result.id
            + " " + result.title
            + " | 期待: " + result.expected
            + " | 実際: " + result.actual;
        if (!result.knownBug.empty()) {
            line += " | 既知バグ: " + result.knownBug;
        }
        LogEvent(line);
    }

    void Report::Log(const std::string& line)
    {
        log_.push_back(line);
        while (log_.size() > kMaxLogLines) {
            log_.pop_front();
        }
    }

    bool Report::ConsumeRestartRequest()
    {
        const bool requested = restartRequested_;
        restartRequested_ = false;
        return requested;
    }

    void LogEvent(const std::string& line)
    {
        auto& report = Report::Get();
        const std::string formatted = "[f" + std::to_string(report.GetFrame()) + "] " + line;
        report.Log(formatted);

        // ログファイル / Console UI へも流す（spdlog は終了までフラッシュされないため
        // 実行中の確認は Console UI かこのパネルを見る）
        CoreEngine::Logger::GetInstance().Log(
            "[CollisionTest] " + formatted,
            CoreEngine::LogLevel::Info,
            CoreEngine::LogCategory::Game,
            CoreEngine::LogSubCategory::Physics);
    }

#ifdef USE_IMGUI

    void Report::EnsureRegistered(CoreEngine::EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }

        auto* debug = engine->GetDebugSubsystem();
        auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
        if (!gameDebugUI) {
            return;
        }

        // ドロワーは何もキャプチャしない（シングルトンを読むだけ）。
        // GameDebugUI に App Editor の登録解除 API が無いため、シーンの this を
        // キャプチャするとシーン破棄後にダングリングする。
        gameDebugUI->RegisterAppEditor("Collision Test", [] {
            Report::Get().Draw();
            });

        registered = true;
    }

    void Report::Draw()
    {
        using namespace CoreEngine;

        UI::SectionHeader("Session");
        UI::Label(sessionName_.empty() ? "(未開始)" : sessionName_.c_str());
        UI::HintF("frame: %d", frame_);

        int pass = 0, fail = 0, pending = 0;
        for (const auto& c : cases_) {
            if (c.status == Status::Pass)      { ++pass; }
            else if (c.status == Status::Fail) { ++fail; }
            else                              { ++pending; }
        }
        ImGui::Text("PASS %d / FAIL %d / 進行中 %d", pass, fail, pending);
        ImGui::SameLine();
        UI::HelpMarker(
            "FAIL のうち Known Bug 列に番号があるものは、リファクタリング計画書\n"
            "（Docs/Engine/Collision/Collision_Refactoring_Plan.md）で挙げた\n"
            "既知バグの再現です。Phase 1 完了後に PASS へ変わることが完了条件。");

        if (ImGui::Button("テストをリスタート")) {
            restartRequested_ = true;
        }

        UI::SectionHeader("Test Cases");
        if (ImGui::BeginTable("CollisionTestCases", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("状態", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("内容");
            ImGui::TableSetupColumn("期待");
            ImGui::TableSetupColumn("実際");
            ImGui::TableSetupColumn("Known Bug", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableHeadersRow();

            for (const auto& c : cases_) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.id.c_str());

                ImGui::TableNextColumn();
                const ImVec4 color =
                    (c.status == Status::Pass) ? ImVec4(0.40f, 0.85f, 0.45f, 1.0f) :
                    (c.status == Status::Fail) ? ImVec4(0.95f, 0.40f, 0.35f, 1.0f) :
                                                 ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
                ImGui::TextColored(color, "%s", ToLabel(c.status));

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.title.c_str());
                if (!c.note.empty()) {
                    ImGui::SameLine();
                    UI::HelpMarker(c.note.c_str());
                }

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.expected.c_str());

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.actual.c_str());

                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.knownBug.empty() ? "-" : c.knownBug.c_str());
            }
            ImGui::EndTable();
        }

        UI::SectionHeader("危険な再現（オプトイン）");
        ImGui::Checkbox("A-2: コールバック中に RemoveCollider() を呼ぶ",
            &removeColliderInCallback_);
        UI::Hint("解放済みコライダーを判定ループが触るため、クラッシュする可能性があります。");

        UI::SectionHeader("Event Log");
        if (ImGui::BeginChild("CollisionTestLog", ImVec2(0.0f, 220.0f),
            ImGuiChildFlags_Borders)) {
            for (const auto& line : log_) {
                ImGui::TextUnformatted(line.c_str());
            }
            // 新しい行が追加されたら追従スクロール
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }

#endif // USE_IMGUI
}
