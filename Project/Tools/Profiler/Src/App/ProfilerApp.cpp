#include "App/ProfilerApp.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Platform/FileDialog.h"
#include "Text/TextFormat.h"

#include <imgui.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

namespace CoreEngine::Profiler
{
    namespace
    {
        namespace Theme = CoreEngine::Editor::Theme;

        constexpr const char* kDiscardPopupId = "保存していない記録";
        constexpr const char* kConnectPopupId = "つなぐ";

        /// @brief "アドレス:ポート" か "アドレス" を分ける。ポートが無いか読めなければ port はそのまま
        void ParseEndpoint(const std::string& text, std::string& address, int& port)
        {
            const size_t colon = text.rfind(':');
            if (colon == std::string::npos) {
                if (!text.empty()) {
                    address = text;
                }
                return;
            }
            if (colon > 0) {
                address = text.substr(0, colon);
            }
            const int parsed = std::atoi(text.c_str() + colon + 1);
            if (parsed > 0 && parsed <= 65535) {
                port = parsed;
            }
        }

        /// @brief 状態の言葉と印の色
        void StateLabel(SessionState state, bool saving, const char*& label, ImVec4& color)
        {
            if (saving) {
                label = "保存しています…";
                color = Theme::kWarm;
                return;
            }
            switch (state) {
            case SessionState::Connecting: label = "つないでいます…"; color = Theme::kWarm; break;
            case SessionState::Live: label = "接続中"; color = Theme::kOk; break;
            case SessionState::Stopped: label = "止めた"; color = Theme::kTextDim; break;
            case SessionState::Loading: label = "読み込んでいます…"; color = Theme::kWarm; break;
            case SessionState::File: label = "ファイル"; color = Theme::kAccentHover; break;
            default: label = "記録なし"; color = Theme::kTextMute; break;
            }
        }

        /// @brief 文字の高さの真ん中に丸い印を描いて、その分だけ右へ進める
        void StatusDot(const ImVec4& color)
        {
            const float height = ImGui::GetFrameHeight();
            const float radius = ImGui::GetTextLineHeight() * 0.28f;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + radius, pos.y + height * 0.5f), radius,
                ImGui::GetColorU32(color));
            ImGui::Dummy(ImVec2(radius * 2.0f, height));
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        }

        /// @brief 「・」で区切った薄い文字を同じ行へ足す
        void DetailText(const std::string& text)
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
            ImGui::TextUnformatted("　・　");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
        }

        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        /// @brief 強調したボタン（青の地）
        bool AccentButton(const char* label)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::FromSrgb(255, 255, 255));
            const bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(4);
            return pressed;
        }

        /// @brief 保存の窓に最初に入れる名前（相手の exe の名前と今の日時）
        std::wstring DefaultSaveName(const SessionSummary& summary, bool hasSummary)
        {
            std::string program = hasSummary ? summary.program : std::string();
            if (const size_t dot = program.rfind('.'); dot != std::string::npos) {
                program.resize(dot);
            }
            if (program.empty()) {
                program = "trace";
            }
            SYSTEMTIME time = {};
            ::GetLocalTime(&time);
            const std::string name = std::format("{}_{:04}{:02}{:02}_{:02}{:02}{:02}.tracy", program,
                time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
            return Text::ToWide(name);
        }
    }

    ProfilerApp::ProfilerApp(const std::vector<std::wstring>& arguments)
    {
        bool connectOnStart = false;
        std::string openPath;
        for (size_t i = 0; i < arguments.size(); ++i) {
            const std::wstring& argument = arguments[i];
            if (argument == L"--connect") {
                connectOnStart = true;
                if (i + 1 < arguments.size() && !arguments[i + 1].starts_with(L"--")) {
                    ParseEndpoint(Text::ToUtf8(arguments[++i]), connectAddress_, connectPort_);
                }
            } else if (argument == L"--open") {
                if (i + 1 < arguments.size()) {
                    openPath = Text::ToUtf8(arguments[++i]);
                }
            } else if (!argument.starts_with(L"--")) {
                openPath = Text::ToUtf8(argument);
            }
        }

        if (!openPath.empty()) {
            session_.Open(openPath);
        } else if (connectOnStart) {
            session_.Connect(connectAddress_, static_cast<uint16_t>(connectPort_));
        }
    }

    std::wstring ProfilerApp::WindowTitle() const
    {
        std::wstring title = L"CoreEngine Profiler";
        if (session_.State() == SessionState::File && !session_.FilePath().empty()) {
            title += L" — " + Text::ToWide(Text::FileName(session_.FilePath()));
        } else if (hasSummary_ && !summary_.program.empty()) {
            title += L" — " + Text::ToWide(summary_.program);
        }
        return title;
    }

    void ProfilerApp::RequestQuit()
    {
        Request(PendingAction::Quit);
    }

    void ProfilerApp::RequestOpen(const std::string& path)
    {
        pendingPath_ = path;
        Request(PendingAction::OpenPath);
    }

    void ProfilerApp::Request(PendingAction action)
    {
        if (session_.IsBusy() && action != PendingAction::Quit) {
            return;
        }
        if (session_.HasUnsavedData()) {
            pending_ = action;
            openDiscardPopup_ = true;
            return;
        }
        Run(action);
    }

    void ProfilerApp::Run(PendingAction action)
    {
        switch (action) {
        case PendingAction::Connect:
            session_.Connect(connectAddress_, static_cast<uint16_t>(connectPort_));
            break;
        case PendingAction::OpenDialog:
            openDialogRequested_ = true;
            break;
        case PendingAction::OpenPath:
            session_.Open(pendingPath_);
            break;
        case PendingAction::Quit:
            quit_ = true;
            break;
        default:
            break;
        }
    }

    void ProfilerApp::AfterFrame(HWND owner)
    {
        if (openDialogRequested_) {
            openDialogRequested_ = false;
            if (const auto path = Platform::ShowOpenTraceDialog(owner)) {
                session_.Open(Text::ToUtf8(*path));
            }
        }
        if (saveDialogRequested_) {
            saveDialogRequested_ = false;
            if (const auto path = Platform::ShowSaveTraceDialog(owner, DefaultSaveName(summary_, hasSummary_))) {
                session_.Save(Text::ToUtf8(*path));
            }
        }
    }

    void ProfilerApp::Frame()
    {
        session_.Update();
        if (session_.Generation() != seenGeneration_) {
            seenGeneration_ = session_.Generation();
            statsView_.Reset();
            hasSummary_ = false;
        }
        hasSummary_ = session_.ReadSummary(summary_);
        const bool live = session_.State() == SessionState::Live;
        const double now = ImGui::GetTime();
        session_.Read([&](const tracy::Worker& worker) {
            if (worker.HasData()) {
                statsView_.Update(worker, live, now);
            }
        });

        HandleShortcuts();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##root", nullptr, rootFlags);
        ImGui::PopStyleVar(2);

        DrawMenuBar();
        DrawConnectionStrip();
        DrawNotice();
        ImGui::Spacing();

        switch (session_.State()) {
        case SessionState::Empty:
            DrawEmptyState();
            break;
        case SessionState::Connecting:
        case SessionState::Loading:
            DrawWaiting();
            break;
        default:
            statsView_.Draw();
            break;
        }

        DrawConnectPopup();
        DrawDiscardPopup();
        ImGui::End();
    }

    void ProfilerApp::HandleShortcuts()
    {
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            return;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
            Request(PendingAction::OpenDialog);
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && session_.CanSave()) {
            saveDialogRequested_ = true;
        }
    }

    void ProfilerApp::DrawMenuBar()
    {
        if (!ImGui::BeginMenuBar()) {
            return;
        }
        const bool busy = session_.IsBusy();
        const SessionState state = session_.State();

        if (ImGui::BeginMenu("ファイル")) {
            if (ImGui::MenuItem("開く…", "Ctrl+O", false, !busy)) {
                Request(PendingAction::OpenDialog);
            }
            if (ImGui::MenuItem("保存…", "Ctrl+S", false, session_.CanSave())) {
                saveDialogRequested_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("終わる")) {
                RequestQuit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("記録")) {
            if (ImGui::MenuItem("つなぐ…", nullptr, false, !busy)) {
                openConnectPopup_ = true;
            }
            const bool connecting = state == SessionState::Connecting;
            if (ImGui::MenuItem(connecting ? "探すのをやめる" : "記録を止める", nullptr, false,
                connecting || state == SessionState::Live)) {
                session_.Stop();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("ヘルプ")) {
            ImGui::BeginDisabled();
            ImGui::MenuItem("保存した .tracy は元の Tracy（0.15.0）でも開けます");
            ImGui::EndDisabled();
            if (ImGui::MenuItem("元の Tracy の入手先を開く")) {
                ::ShellExecuteW(nullptr, L"open", L"https://github.com/wolfpld/tracy/releases", nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    void ProfilerApp::DrawConnectionStrip()
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kPanel);
        const float height = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        ImGui::BeginChild("##strip", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        const SessionState state = session_.State();
        const char* label = nullptr;
        ImVec4 color;
        StateLabel(state, session_.IsSaving(), label, color);
        StatusDot(color);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();

        switch (state) {
        case SessionState::Connecting:
            DetailText(std::format("{}:{}", session_.Address(), session_.Port()));
            break;
        case SessionState::Loading:
            break;
        case SessionState::Live:
        case SessionState::Stopped:
            if (hasSummary_) {
                DetailText(std::format("{}（PID {}）", summary_.program, summary_.pid));
                DetailText(std::format("{}:{}", session_.Address(), session_.Port()));
            }
            break;
        case SessionState::File:
            DetailText(std::string(Text::FileName(session_.FilePath())));
            if (hasSummary_ && !summary_.program.empty()) {
                DetailText(summary_.program);
            }
            break;
        default:
            break;
        }
        if (hasSummary_ && (state == SessionState::Live || state == SessionState::Stopped || state == SessionState::File)) {
            DetailText("記録 " + Text::FormatClock(summary_.span));
            DetailText(Text::FormatCount(summary_.frameCount) + " フレーム");
        }

        // 右端のボタン
        const bool busy = session_.IsBusy();
        const char* stopLabel = (state == SessionState::Connecting) ? "やめる" : "記録を止める";
        const bool showStop = state == SessionState::Live || state == SessionState::Connecting;
        const bool showSave = state == SessionState::Stopped || state == SessionState::File;
        float width = ButtonWidth("開く…");
        if (showStop) {
            width += ButtonWidth(stopLabel) + style.ItemSpacing.x;
        }
        if (showSave) {
            width += ButtonWidth("保存…") + style.ItemSpacing.x;
        }
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x * 2.0f, right - width));
        if (showStop) {
            if (ImGui::Button(stopLabel)) {
                session_.Stop();
            }
            ImGui::SameLine();
        }
        if (showSave) {
            ImGui::BeginDisabled(!session_.CanSave());
            const bool emphasize = session_.HasUnsavedData();
            if (emphasize ? AccentButton("保存…") : ImGui::Button("保存…")) {
                saveDialogRequested_ = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("開く…")) {
            Request(PendingAction::OpenDialog);
        }
        ImGui::EndDisabled();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void ProfilerApp::DrawNotice()
    {
        const std::string& notice = session_.Notice();
        if (notice.empty()) {
            return;
        }
        ImVec4 color = Theme::kTextDim;
        switch (session_.NoticeLevelOf()) {
        case NoticeLevel::Warning: color = Theme::kWarn; break;
        case NoticeLevel::Error: color = Theme::kError; break;
        default: break;
        }
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(notice.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("閉じる")) {
            session_.ClearNotice();
        }
    }

    void ProfilerApp::DrawEmptyState()
    {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() * 2.0f));
        ImGui::Indent(ImGui::GetTextLineHeight());
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
        ImGui::TextUnformatted("記録がありません");
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetTextLineHeight() * 60.0f);
        ImGui::TextUnformatted("エディタやゲームを Debug か Development で起動してから「つなぐ」を押すと、その記録を受け取ります。");
        ImGui::TextUnformatted("保存した記録（.tracy）は「開く…」か、この窓へドラッグして開けます。");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const std::string connectLabel = std::format("{}:{} につなぐ", connectAddress_, connectPort_);
        if (AccentButton(connectLabel.c_str())) {
            Request(PendingAction::Connect);
        }
        ImGui::SameLine();
        if (ImGui::Button("記録を開く…")) {
            Request(PendingAction::OpenDialog);
        }
        ImGui::Unindent(ImGui::GetTextLineHeight());
    }

    void ProfilerApp::DrawWaiting()
    {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight() * 2.0f));
        ImGui::Indent(ImGui::GetTextLineHeight());
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        if (session_.State() == SessionState::Connecting) {
            ImGui::Text("%s:%d の相手を探しています。", session_.Address().c_str(), session_.Port());
            ImGui::TextUnformatted("エディタやゲームを Debug か Development で起動すると、自動でつながります。");
        } else {
            ImGui::TextUnformatted("記録を読み込んでいます…");
        }
        ImGui::PopStyleColor();
        ImGui::Unindent(ImGui::GetTextLineHeight());
    }

    void ProfilerApp::DrawConnectPopup()
    {
        if (openConnectPopup_) {
            openConnectPopup_ = false;
            std::snprintf(addressBuffer_, sizeof(addressBuffer_), "%s", connectAddress_.c_str());
            ImGui::OpenPopup(kConnectPopupId);
        }
        if (!ImGui::BeginPopupModal(kConnectPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTextDim);
        ImGui::TextUnformatted("つなぐ相手のアドレスとポート。エンジンは同じ PC からだけ受け付けます。");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(ImGui::GetTextLineHeight() * 12.0f);
        ImGui::InputText("アドレス", addressBuffer_, sizeof(addressBuffer_));
        ImGui::SetNextItemWidth(ImGui::GetTextLineHeight() * 12.0f);
        if (ImGui::InputInt("ポート", &connectPort_, 0, 0)) {
            connectPort_ = std::clamp(connectPort_, 1, 65535);
        }
        ImGui::Spacing();
        if (AccentButton("つなぐ")) {
            connectAddress_ = addressBuffer_;
            ImGui::CloseCurrentPopup();
            Request(PendingAction::Connect);
        }
        ImGui::SameLine();
        if (ImGui::Button("やめる") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void ProfilerApp::DrawDiscardPopup()
    {
        if (openDiscardPopup_) {
            openDiscardPopup_ = false;
            ImGui::OpenPopup(kDiscardPopupId);
        }
        if (!ImGui::BeginPopupModal(kDiscardPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        const bool quitting = pending_ == PendingAction::Quit;
        ImGui::TextUnformatted("受け取った記録をまだ保存していません。");
        ImGui::TextUnformatted(quitting ? "保存せずに終わりますか？" : "記録を捨てて続けますか？");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kError);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::FromSrgb(236, 112, 102));
        const bool discard = ImGui::Button(quitting ? "保存せずに終わる" : "捨てて続ける");
        ImGui::PopStyleColor(2);
        if (discard) {
            const PendingAction action = pending_;
            pending_ = PendingAction::None;
            ImGui::CloseCurrentPopup();
            Run(action);
        }
        ImGui::SameLine();
        if (ImGui::Button("やめる") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pending_ = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
