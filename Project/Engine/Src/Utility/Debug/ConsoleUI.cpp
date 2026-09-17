#include "pch.h"
#include "ConsoleUI.h"

#ifdef USE_IMGUI
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Light/LightManager.h"
#include "Input/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Script/ScriptSubsystem.h"

// コンポーネントのインクルード
#include "Editor/External/ExternalCodeEditor.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Utility/CVar/CVarConsole.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <format>
#include <unordered_map>


namespace CoreEngine
{

namespace Theme = Editor::Theme;

// カテゴリの絞り込み（先頭は「すべて」）
static const char* const kCategoryNames[] = {
    "すべて", "System", "Graphics", "Resource", "Shader", "Audio", "Game", "Script", "General", "Console"
};
static const char* const kCategoryFilters[] = {
    nullptr, "System", "Graphics", "Resource", "Shader", "Audio", "Game", "Script", "General", "Console"
};
static constexpr int kCategoryCount = 10;

// 補完に使う組み込みコマンド
static const char* const kCommandNames[] = { "help", "clear", "fps", "status", "exit", "cvar" };

/// @brief 本文から「スクリプトのファイル(行, 桁)」を探す
/// @details コンパイラの `Foo.as(23, 9) : ...` と、実行時の `@ Foo.as(23, 9)` の形を拾う。
static void FindScriptLocation(const std::string& text, std::string& file, int& line, int& column)
{
    const size_t mark = text.find(".as(");
    if (mark == std::string::npos) {
        return;
    }

    // ファイル名の先頭（空白・@・引用符の直後）まで戻る
    size_t begin = mark;
    while (begin > 0) {
        const char c = text[begin - 1];
        if (c == ' ' || c == '\t' || c == '@' || c == '\'' || c == '"' || c == '[' || c == '(') {
            break;
        }
        --begin;
    }
    if (begin == mark) {
        return;
    }

    // (行, 桁)
    size_t at = mark + 4;
    const auto readNumber = [&text, &at](int& out) {
        bool any = false;
        out = 0;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
            out = out * 10 + (text[at] - '0');
            ++at;
            any = true;
        }
        return any;
    };

    int row = 0;
    if (!readNumber(row)) {
        return;
    }
    int col = 0;
    if (at < text.size() && text[at] == ',') {
        ++at;
        while (at < text.size() && text[at] == ' ') {
            ++at;
        }
        readNumber(col);
    }

    file = text.substr(begin, mark + 3 - begin);
    line = row;
    column = col;
}

ConsoleMessage::ConsoleMessage(const std::string& msg, ConsoleLogLevel lvl, const std::string& cat)
    : message(msg), category(cat), level(lvl), timestamp(std::chrono::system_clock::now())
{
    // タイムスタンプを構築時に計算（spdlog非同期スレッドで実行され、メインスレッドの負荷を軽減）
    auto tt = std::chrono::system_clock::to_time_t(timestamp);
    int ms_val = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[16];
    sprintf_s(buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms_val);
    formattedTimestamp = buf;

    FindScriptLocation(message, sourceFile, sourceLine, sourceColumn);
}

void ConsoleUI::Initialize()
{
    // 初期メッセージを追加
    AddLog("コンソールが初期化されました", ConsoleLogLevel::Info);
    AddLog("デバッグコンソールが使用可能です", ConsoleLogLevel::Debug);
}

void ConsoleUI::Draw()
{
    if (!isVisible_) return;

    // 保留中のメッセージをメインキューに転送
    FlushPendingMessages();

    // 絞り込みが変わったら、件数と表示する行を作り直す
    bool changed = viewDirty_;
    for (int i = 0; i < kLevelCount; ++i) {
        if (prevShowLevel_[i] != showLevel_[i]) {
            prevShowLevel_[i] = showLevel_[i];
            changed = true;
        }
    }
    if (prevCategoryFilter_ != categoryFilter_ || prevCollapse_ != collapse_) {
        prevCategoryFilter_ = categoryFilter_;
        prevCollapse_ = collapse_;
        changed = true;
    }
    if (strcmp(prevFilterBuf_, filter_.InputBuf) != 0) {
        snprintf(prevFilterBuf_, sizeof(prevFilterBuf_), "%s", filter_.InputBuf);
        changed = true;
    }
    if (changed) {
        RebuildView();
        viewDirty_ = false;
    }

    if (focusWindow_) {
        ImGui::SetNextWindowFocus();
        focusWindow_ = false;
    }

    if (auto w = UI::Scope::WindowScope("Console")) {
        DrawToolbar();
        DrawRows();
        DrawCommandInput();
    }
}

void ConsoleUI::DrawToolbar()
{
    // レベルごとの件数を兼ねた表示の切り替え
    for (int i = 0; i < kLevelCount; ++i) {
        const auto level = static_cast<ConsoleLogLevel>(i);
        const std::string label = std::format("{} {}", GetLevelString(level), levelCounts_[i]);
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const float dotRadius = 3.5f;
        const ImVec2 size(textSize.x + 30.0f, ImGui::GetFrameHeight());

        if (i > 0) {
            UI::SameLine(0.0f, 4.0f);
        }
        ImGui::PushID(i);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##level", size);
        const ImVec2 max(min.x + size.x, min.y + size.y);
        UI::Bar::detail::DrawButtonFrame(min, max, false, ImGui::IsItemHovered(), ImGui::IsItemActive());

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const bool on = showLevel_[i];
        const ImVec4 dotColor = on ? GetMessageColor(level) : Theme::kTextMute;
        draw->AddCircleFilled(ImVec2(min.x + 12.0f, min.y + size.y * 0.5f), dotRadius, ImGui::GetColorU32(dotColor));
        draw->AddText(ImVec2(min.x + 22.0f, min.y + (size.y - textSize.y) * 0.5f),
            ImGui::GetColorU32(on ? Theme::kText : Theme::kTextMute), label.c_str());

        if (pressed) {
            showLevel_[i] = !showLevel_[i];
        }
        UI::Tooltip(on ? "クリックで隠す" : "クリックで出す");
        ImGui::PopID();
    }

    UI::Bar::Separator();
    if (UI::Bar::Button("クリア", false)) {
        ClearLog();
    }
    UI::SameLine(0.0f, 4.0f);
    if (UI::Bar::Button("同じ行を畳む", collapse_, "同じ内容のログを 1 行にまとめ、件数を出す")) {
        collapse_ = !collapse_;
    }
    UI::SameLine(0.0f, 4.0f);
    if (UI::Bar::Button("エラーで一時停止", pauseOnError_, "再生中にエラーが出たら一時停止する")) {
        pauseOnError_ = !pauseOnError_;
    }

    UI::Bar::Separator();
    ImGui::SetNextItemWidth(110.0f);
    if (auto combo = UI::Scope::ComboScope("##category", kCategoryNames[categoryFilter_])) {
        for (int i = 0; i < kCategoryCount; ++i) {
            if (ImGui::Selectable(kCategoryNames[i], i == categoryFilter_)) {
                categoryFilter_ = i;
            }
        }
    }

    UI::SameLine(0.0f, 6.0f);
    const float optionsWidth = UI::Bar::ButtonWidth("表示 ▾");
    ImGui::SetNextItemWidth((std::max)(80.0f, ImGui::GetContentRegionAvail().x - optionsWidth - 6.0f));
    if (ImGui::InputTextWithHint("##filter", "フィルタ…", filter_.InputBuf, IM_ARRAYSIZE(filter_.InputBuf))) {
        filter_.Build();
    }

    UI::SameLine(0.0f, 6.0f);
    if (UI::Bar::Button("表示 ▾", false)) {
        ImGui::OpenPopup("##consoleOptions");
    }
    if (auto popup = UI::Scope::PopupScope("##consoleOptions")) {
        UI::Widgets::ToggleSwitch("時刻を出す", &showTimestamp_);
        UI::Widgets::ToggleSwitch("新しいログへ自動で送る", &autoScroll_);
    }
}

void ConsoleUI::DrawRows()
{
    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg
        | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable;

    auto table = UI::Scope::TableScope("##logRows", 3, flags, ImVec2(0.0f, -footerHeight));
    if (!table) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 0);
    ImGui::TableSetupColumn("レベル", ImGuiTableColumnFlags_WidthFixed, 62.0f);
    ImGui::TableSetupColumn("本文", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("発生元", ImGuiTableColumnFlags_WidthFixed, 190.0f);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(viewRows_.size()));
    while (clipper.Step()) {
        for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex) {
            const ViewRow& row = viewRows_[static_cast<size_t>(rowIndex)];
            const ConsoleMessage& message = messages_[row.index];

            ImGui::TableNextRow();
            ImGui::PushID(rowIndex);

            // 行全体を選べる面（ダブルクリックで発生元を開く・右クリックで写す）
            ImGui::TableSetColumnIndex(0);
            ImGui::Selectable("##row", false,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
                | ImGuiSelectableFlags_AllowDoubleClick);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                OpenSource(message);
            }
            if (auto menu = UI::Scope::PopupContextItemScope("##rowMenu")) {
                if (ImGui::MenuItem("本文をコピー")) {
                    ImGui::SetClipboardText(message.message.c_str());
                }
                if (!message.sourceFile.empty() && ImGui::MenuItem("VS Code で開く")) {
                    OpenSource(message);
                }
            }

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(GetMessageColor(message.level), "[%s]", GetLevelString(message.level));

            // 本文（畳んだ件数と時刻を前に付ける）
            ImGui::TableSetColumnIndex(1);
            if (row.count > 1) {
                ImGui::TextColored(Theme::kWarm, "×%zu", row.count);
                ImGui::SameLine(0.0f, 6.0f);
            }
            if (showTimestamp_) {
                ImGui::TextColored(Theme::kTextMute, "%s", message.formattedTimestamp.c_str());
                ImGui::SameLine(0.0f, 6.0f);
            }
            ImGui::TextUnformatted(message.message.c_str());

            // 発生元（スクリプトの行が分かれば押して開ける）
            ImGui::TableSetColumnIndex(2);
            if (!message.sourceFile.empty()) {
                const std::string where = std::format("{}:{}", message.sourceFile, message.sourceLine);
                ImGui::TextColored(Theme::kAccentHover, "%s", where.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("クリックで VS Code の %d 行目を開く", message.sourceLine);
                }
                if (ImGui::IsItemClicked()) {
                    OpenSource(message);
                }
            } else {
                ImGui::TextColored(Theme::kTextMute, "%s", message.category.c_str());
            }

            ImGui::PopID();
        }
    }
    clipper.End();

    // 自動スクロール
    if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
}

void ConsoleUI::DrawCommandInput()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Theme::kAccentHover, ">");
    UI::SameLine(0.0f, 6.0f);

    if (focusInput_) {
        ImGui::SetKeyboardFocusHere();
        focusInput_ = false;
    }

    const float runWidth = UI::Bar::ButtonWidth("実行");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - runWidth - 8.0f);
    constexpr ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
    const bool enterPressed = UI::InputText("##CommandInput", inputBuffer_, sizeof(inputBuffer_),
                                            inputFlags, &ConsoleUI::InputTextCallback, this);

    UI::SameLine(0.0f, 8.0f);
    const bool runPressed = UI::Bar::Button("実行", false, "help でコマンドの一覧");
    if ((runPressed || enterPressed) && inputBuffer_[0] != '\0') {
        std::string command(inputBuffer_);
        ProcessCommand(command);
        inputBuffer_[0] = '\0';
        focusInput_ = true;
    }
}

void ConsoleUI::OpenSource(const ConsoleMessage& message) const
{
    if (message.sourceFile.empty()) {
        return;
    }

    // コンパイラのメッセージのファイル名は、スクリプトのフォルダからの相対パス
    std::filesystem::path file = Logger::GetInstance().Utf8ToPath(message.sourceFile);
    if (file.is_relative() && engine_) {
        if (ScriptSubsystem* const script = engine_->GetSubsystem<ScriptSubsystem>()) {
            file = script->GetScriptRoot() / file;
        }
    }
    Editor::OpenInCodeEditor(file, message.sourceLine, message.sourceColumn);
}

void ConsoleUI::AddLog(const std::string& message, ConsoleLogLevel level)
{
    AddLog(message, level, "Console");
}

void ConsoleUI::AddLog(const std::string& message, ConsoleLogLevel level, const std::string& category)
{
    // メッセージをロック外で構築（タイムスタンプ計算を含む）
    // → spdlog 非同期スレッドで並列に実行される
    ConsoleMessage msg(message, level, category);
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingMessages_.push_back(std::move(msg));
}

void ConsoleUI::ClearLog()
{
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingMessages_.clear();
    }
    messages_.clear();
    viewDirty_ = true;
    AddLog("コンソールログをクリアしました", ConsoleLogLevel::Info);
}

void ConsoleUI::LogInfo(const std::string& message)
{
    AddLog(message, ConsoleLogLevel::Info);
}

void ConsoleUI::LogWarning(const std::string& message)
{
    AddLog(message, ConsoleLogLevel::Warning);
}

void ConsoleUI::LogError(const std::string& message)
{
    AddLog(message, ConsoleLogLevel::Error);
}

void ConsoleUI::LogDebug(const std::string& message)
{
    AddLog(message, ConsoleLogLevel::Debug);
}

ImVec4 ConsoleUI::GetMessageColor(ConsoleLogLevel level) const
{
    switch (level) {
    case ConsoleLogLevel::Info:    return Theme::kText;
    case ConsoleLogLevel::Warning: return Theme::kWarn;
    case ConsoleLogLevel::Error:   return Theme::kError;
    case ConsoleLogLevel::Debug:   return Theme::kTextMute;
    default:                       return Theme::kText;
    }
}

const char* ConsoleUI::GetLevelString(ConsoleLogLevel level) const
{
    switch (level) {
    case ConsoleLogLevel::Info:    return "情報";
    case ConsoleLogLevel::Warning: return "警告";
    case ConsoleLogLevel::Error:   return "エラー";
    case ConsoleLogLevel::Debug:   return "デバッグ";
    default:                       return "?";
    }
}

bool ConsoleUI::ShouldShowMessage(const ConsoleMessage& message) const
{
    const int level = static_cast<int>(message.level);
    if (level >= 0 && level < kLevelCount && !showLevel_[level]) {
        return false;
    }
    const char* const category = kCategoryFilters[categoryFilter_];
    return category == nullptr || message.category == category;
}

void ConsoleUI::ProcessCommand(const std::string& command)
{
    if (command.empty()) {
        return;
    }

    // コマンドをログに表示
    AddLog("> " + command, ConsoleLogLevel::Debug);

    // 履歴へ積む（同じコマンドが続いたときは 1 つだけ持つ）
    if (history_.empty() || history_.back() != command) {
        history_.push_back(command);
    }
    historyPos_ = -1;

    // コマンドを空白で分割
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(command);
    while (std::getline(tokenStream, token, ' ')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }

    if (tokens.empty()) {
        return;
    }

    const std::string& cmd = tokens[0];

    // === ヘルプコマンド ===
    if (cmd == "help" || cmd == "h") {
        AddLog("=== 利用可能なコマンド ===", ConsoleLogLevel::Info);
        AddLog("help, h              - このヘルプを表示", ConsoleLogLevel::Info);
        AddLog("clear, cls           - ログをクリア", ConsoleLogLevel::Info);
        AddLog("fps                  - FPS情報を表示", ConsoleLogLevel::Info);
        AddLog("status, stat         - システム状態を表示", ConsoleLogLevel::Info);
        AddLog("exit, quit           - コンソールを閉じる", ConsoleLogLevel::Info);
        AddLog("cvar <接頭辞>        - 名前が一致する CVar と値を一覧", ConsoleLogLevel::Info);
        AddLog("cvar <名前>          - その CVar の型・値・既定値・説明を表示", ConsoleLogLevel::Info);
        AddLog("cvar <名前> <値>     - 値を書き込む（Ctrl+Z で戻せる）", ConsoleLogLevel::Info);
        AddLog("cvar reset <名前>    - 既定値へ戻す", ConsoleLogLevel::Info);
        AddLog("↑ ↓ で履歴、Tab で補完", ConsoleLogLevel::Info);
    }
    // === ログクリアコマンド ===
    else if (cmd == "clear" || cmd == "cls") {
        ClearLog();
        AddLog("ログをクリアしました", ConsoleLogLevel::Info);
    }
    // === FPS情報コマンド ===
    else if (cmd == "fps") {
        ShowFPSInfo();
    }
    // === システム状態コマンド ===
    else if (cmd == "status" || cmd == "stat") {
        ShowSystemStatus();
    }
    // === コンソール終了コマンド ===
    else if (cmd == "exit" || cmd == "quit") {
        SetVisible(false);
        AddLog("コンソールを閉じました", ConsoleLogLevel::Info);
    }
    // === CVar コマンド ===
    else if (const CVarConsole::Result result = CVarConsole::Execute(command); result.handled) {
        for (const std::string& line : result.lines) {
            AddLog(line, result.failed ? ConsoleLogLevel::Error : ConsoleLogLevel::Info);
        }
    }
    // === 不明なコマンド ===
    else {
        AddLog("不明なコマンド: " + cmd + " (help で利用可能なコマンドを表示)", ConsoleLogLevel::Error);
    }
}

int ConsoleUI::InputTextCallback(ImGuiInputTextCallbackData* data)
{
    auto* const console = data ? static_cast<ConsoleUI*>(data->UserData) : nullptr;
    return console ? console->OnInputText(*data) : 0;
}

int ConsoleUI::OnInputText(ImGuiInputTextCallbackData& data)
{
    // ↑ ↓ で履歴をたどる
    if (data.EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        const int previous = historyPos_;
        if (data.EventKey == ImGuiKey_UpArrow) {
            if (historyPos_ < 0) {
                historyPos_ = static_cast<int>(history_.size()) - 1;
            } else if (historyPos_ > 0) {
                --historyPos_;
            }
        } else if (data.EventKey == ImGuiKey_DownArrow && historyPos_ >= 0) {
            if (++historyPos_ >= static_cast<int>(history_.size())) {
                historyPos_ = -1;
            }
        }
        if (previous != historyPos_) {
            const std::string line =
                historyPos_ >= 0 ? history_[static_cast<size_t>(historyPos_)] : std::string();
            data.DeleteChars(0, data.BufTextLen);
            data.InsertChars(0, line.c_str());
        }
        return 0;
    }

    // Tab で補完する
    if (data.EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        const std::vector<std::string> candidates = CollectCompletions(data.Buf);
        if (candidates.empty()) {
            return 0;
        }

        // 候補に共通する所まで入れる
        std::string shared = candidates.front();
        for (const std::string& candidate : candidates) {
            size_t same = 0;
            while (same < shared.size() && same < candidate.size() && shared[same] == candidate[same]) {
                ++same;
            }
            shared.resize(same);
        }
        if (!shared.empty()) {
            data.DeleteChars(0, data.BufTextLen);
            data.InsertChars(0, shared.c_str());
            if (candidates.size() == 1) {
                data.InsertChars(data.CursorPos, " ");
            }
        }

        if (candidates.size() > 1) {
            constexpr size_t kShowLimit = 20;
            AddLog("候補 " + std::to_string(candidates.size()) + " 件:", ConsoleLogLevel::Debug);
            for (size_t i = 0; i < candidates.size() && i < kShowLimit; ++i) {
                AddLog("  " + candidates[i], ConsoleLogLevel::Debug);
            }
            if (candidates.size() > kShowLimit) {
                AddLog("  … 他 " + std::to_string(candidates.size() - kShowLimit) + " 件",
                    ConsoleLogLevel::Debug);
            }
        }
    }
    return 0;
}

std::vector<std::string> ConsoleUI::CollectCompletions(const char* text) const
{
    std::string line(text ? text : "");
    if (const size_t begin = line.find_first_not_of(" \t"); begin == std::string::npos) {
        line.clear();
    } else {
        line = line.substr(begin);
    }

    // 語を打ち終えているなら、その語のコマンドに続く候補を出す
    if (line.find(' ') != std::string::npos) {
        return CVarConsole::Complete(line, 30);
    }

    std::vector<std::string> candidates;
    for (const char* const name : kCommandNames) {
        if (line.empty() || std::string(name).rfind(line, 0) == 0) {
            candidates.emplace_back(name);
        }
    }
    return candidates;
}

void ConsoleUI::ShowFPSInfo()
{
    if (!engine_) {
        AddLog("エラー: エンジンシステムが設定されていません", ConsoleLogLevel::Error);
        return;
    }

    // 【Phase 4】新方式でコンポーネントを取得
    auto frameRate = engine_->GetService<FrameRateController>();
    if (!frameRate) {
        AddLog("エラー: フレームレートコントローラーが利用できません", ConsoleLogLevel::Error);
        return;
    }

    AddLog("=== FPS情報 ===", ConsoleLogLevel::Info);
    
    float currentFPS = frameRate->GetCurrentFPS();
    float targetFPS = frameRate->GetTargetFPS();
    float deltaTime = Time::UnscaledDeltaTime() * 1000.0f; // ms
    
    AddLog("現在のFPS: " + std::to_string(static_cast<int>(currentFPS)) + " FPS", ConsoleLogLevel::Info);
    AddLog("目標FPS: " + std::to_string(static_cast<int>(targetFPS)) + " FPS (固定)", ConsoleLogLevel::Info);
    AddLog("フレーム時間: " + std::to_string(deltaTime) + " ms", ConsoleLogLevel::Info);
    
    // FPS達成率
    float achievementRate = (currentFPS / targetFPS) * 100.0f;
    if (achievementRate >= 95.0f) {
        AddLog("FPS達成率: " + std::to_string(static_cast<int>(achievementRate)) + "% (良好)", ConsoleLogLevel::Info);
    } else if (achievementRate >= 80.0f) {
        AddLog("FPS達成率: " + std::to_string(static_cast<int>(achievementRate)) + "% (やや低下)", ConsoleLogLevel::Warning);
    } else {
        AddLog("FPS達成率: " + std::to_string(static_cast<int>(achievementRate)) + "% (低下)", ConsoleLogLevel::Error);
    }
}

void ConsoleUI::ShowSystemStatus()
{
    if (!engine_) {
        AddLog("エラー: エンジンシステムが設定されていません", ConsoleLogLevel::Error);
        return;
    }

    AddLog("=== システム状態 ===", ConsoleLogLevel::Info);

    // 【Phase 4】コンポーネントの状態チェック
    auto graphicsCore = engine_->GetService<GraphicsCore>();
    auto inputManager = engine_->GetService<InputManager>();
    auto soundManager = engine_->GetService<AudioSystem>();
    auto lightManager = engine_->GetService<LightManager>();

    AddLog("グラフィックスシステム: " + std::string(graphicsCore ? "初期化済み" : "未初期化"), 
           graphicsCore ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("入力システム: " + std::string(inputManager ? "初期化済み" : "未初期化"), 
           inputManager ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("オーディオシステム: " + std::string(soundManager ? "初期化済み" : "未初期化"), 
           soundManager ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("ライティングシステム: " + std::string(lightManager ? "初期化済み" : "未初期化"), 
           lightManager ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("エンジンシステム: 正常稼働中", ConsoleLogLevel::Info);
}

void ConsoleUI::FlushPendingMessages()
{
    // ロック時間を最小限に抑えるため swap で一括取り出し
    std::vector<ConsoleMessage> localMessages;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (pendingMessages_.empty()) return;
        localMessages.swap(pendingMessages_);
    }

    // ロック外で処理（タイムスタンプはコンストラクタで計算済み）
    bool hasError = false;
    for (auto& msg : localMessages) {
        hasError |= (msg.level == ConsoleLogLevel::Error);
        messages_.push_back(std::move(msg));
    }

    while (messages_.size() > maxMessages_) {
        messages_.pop_front();
    }

    viewDirty_ = true;

    // エラーで一時停止
    if (hasError && pauseOnError_) {
        auto& playback = PlaybackStateManager::GetInstance();
        if (playback.IsPlaying()) {
            playback.Pause();
            AddLog("エラーが出たので一時停止しました（⏸ をもう一度押すと再開します）", ConsoleLogLevel::Warning);
        }
    }
}

void ConsoleUI::RebuildView()
{
    // 件数はカテゴリの絞り込みだけを反映する（レベルを隠しても件数は見えるように）
    for (size_t& count : levelCounts_) {
        count = 0;
    }
    const char* const category = kCategoryFilters[categoryFilter_];
    for (const auto& msg : messages_) {
        if (category && msg.category != category) {
            continue;
        }
        const int level = static_cast<int>(msg.level);
        if (level >= 0 && level < kLevelCount) {
            ++levelCounts_[level];
        }
    }

    // 表示する行（畳むときは同じ内容を最初の位置の 1 行にまとめる）
    viewRows_.clear();
    viewRows_.reserve(messages_.size());
    std::unordered_map<std::string, size_t> rowOfContent;
    for (size_t i = 0; i < messages_.size(); ++i) {
        const auto& msg = messages_[i];
        if (!ShouldShowMessage(msg)) {
            continue;
        }
        if (filter_.IsActive() &&
            !filter_.PassFilter(msg.message.c_str()) &&
            !filter_.PassFilter(msg.category.c_str())) {
            continue;
        }

        if (collapse_) {
            std::string key = std::to_string(static_cast<int>(msg.level));
            key += '\x1f';
            key += msg.category;
            key += '\x1f';
            key += msg.message;
            if (const auto found = rowOfContent.find(key); found != rowOfContent.end()) {
                ViewRow& row = viewRows_[found->second];
                ++row.count;
                row.index = i;
                continue;
            }
            rowOfContent.emplace(std::move(key), viewRows_.size());
        }
        viewRows_.push_back({ i, 1 });
    }
}
} // namespace CoreEngine
#endif // USE_IMGUI
