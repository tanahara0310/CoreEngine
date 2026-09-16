#include "pch.h"
#include "ConsoleUI.h"

#ifdef USE_IMGUI
#include "EngineSystem/EngineSystem.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Light/LightManager.h"
#include "Input/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Particle/ParticleSystem.h"

// コンポーネントのインクルード
#include "Utility/CVar/CVarConsole.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"

#include <iomanip>
#include <sstream>
#include <algorithm>


namespace CoreEngine
{

// タブ定義（Draw・RebuildTabCounts・RebuildFilteredView で共有）
static const char* const kTabNames[] = {
    "All", "System", "Graphics", "Resource", "Shader", "Audio", "Game", "Script", "General", "Console"
};
static const char* const kTabCategories[] = {
    nullptr, "System", "Graphics", "Resource", "Shader", "Audio", "Game", "Script", "General", "Console"
};
static const char* const kTabIds[] = {
    "###TabAll", "###TabSystem", "###TabGraphics", "###TabResource",
    "###TabShader", "###TabAudio", "###TabGame", "###TabScript", "###TabGeneral", "###TabConsole"
};
static constexpr int kTabCount = 10;

// 補完に使う組み込みコマンド
static const char* const kCommandNames[] = { "help", "clear", "fps", "status", "exit", "cvar" };

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

    if (auto w = UI::Scope::WindowScope("Console")) {
        // === ヘッダー ===
        if (ImGui::Button("Clear")) {
            ClearLog();
        }
        UI::SameLine();

        if (ImGui::Button("Settings")) {
            ImGui::OpenPopup("ConsoleSettings");
        }

        if (auto popup = UI::Scope::PopupScope("ConsoleSettings")) {
            ImGui::Text("Display Settings");
            UI::Separator();
            UI::Widgets::ToggleSwitch("Auto Scroll", &autoScroll_);
            UI::Widgets::ToggleSwitch("Show Timestamp", &showTimestamp_);

            UI::Separator();
            ImGui::Text("Log Level Filter");
            UI::Widgets::ToggleSwitch("Info",    &showInfo_);
            UI::SameLine();
            UI::Widgets::ToggleSwitch("Warning", &showWarning_);
            UI::SameLine();
            UI::Widgets::ToggleSwitch("Error",   &showError_);
            UI::SameLine();
            UI::Widgets::ToggleSwitch("Debug",   &showDebug_);
        }

        UI::SameLine();
        ImGui::Text("Filter:");
        UI::SameLine();
        filter_.Draw("##Filter", -100.0f);
        UI::SameLine();
        if (ImGui::Button("X")) {
            filter_.Clear();
        }

        UI::Separator();

        // === カテゴリタブ ===
        // フィルター状態変化を検出 → dirty フラグを立てる
        if (prevShowInfo_ != showInfo_ || prevShowWarning_ != showWarning_ ||
            prevShowError_ != showError_ || prevShowDebug_ != showDebug_) {
            prevShowInfo_    = showInfo_;
            prevShowWarning_ = showWarning_;
            prevShowError_   = showError_;
            prevShowDebug_   = showDebug_;
            tabCountsDirty_    = true;
            filteredViewDirty_ = true;
        }
        if (strcmp(prevFilterBuf_, filter_.InputBuf) != 0) {
            snprintf(prevFilterBuf_, sizeof(prevFilterBuf_), "%s", filter_.InputBuf);
            filteredViewDirty_ = true;
        }

        // タブカウントを必要なときだけ再計算（毎フレーム18回全走査を排除）
        if (tabCountsDirty_) {
            RebuildTabCounts();
            tabCountsDirty_ = false;
        }

        if (ImGui::BeginTabBar("##ConsoleTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (int i = 0; i < kTabCount; i++) {
                const size_t count      = cachedTabCounts_[i];
                const size_t errorCount = cachedTabErrorCounts_[i];

                // タブラベル生成（IDは固定、表示名だけ変化）
                char label[128];
                if (errorCount > 0) {
                    snprintf(label, sizeof(label), "%s (%zu) !%s", kTabNames[i], count, kTabIds[i]);
                } else if (count > 0) {
                    snprintf(label, sizeof(label), "%s (%zu)%s", kTabNames[i], count, kTabIds[i]);
                } else {
                    snprintf(label, sizeof(label), "%s%s", kTabNames[i], kTabIds[i]);
                }

                // エラーがある非アクティブタブは色を変える
                bool hasErrors = (errorCount > 0 && i != activeTab_);
                if (hasErrors) {
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
                }

                if (ImGui::BeginTabItem(label)) {
                    activeTab_ = i;
                    ImGui::EndTabItem();
                }

                if (hasErrors) {
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndTabBar();
        }

        // アクティブタブ変化またはビューが dirty なら再構築
        if (filteredViewDirty_ || cachedFilterActiveTab_ != activeTab_) {
            cachedFilterActiveTab_ = activeTab_;
            RebuildFilteredView(kTabCategories[activeTab_]);
            filteredViewDirty_ = false;
        }

        // === メッセージ表示エリア ===
        const float footerHeight = ImGui::GetFrameHeightWithSpacing();
        if (auto child = UI::Scope::ChildScope("ScrollingRegion",
            ImVec2(0, -footerHeight), 0, ImGuiWindowFlags_HorizontalScrollbar)) {

            const bool showCategoryBadge = (activeTab_ == 0); // Allタブのみカテゴリ表示

            // ImGuiListClipper で画面外メッセージの ImGui 呼び出しをスキップ
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filteredIndices_.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& message = messages_[filteredIndices_[static_cast<size_t>(i)]];

                    // タイムスタンプ（事前計算済み文字列を直接使用）
                    if (showTimestamp_) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[%s]", message.formattedTimestamp.c_str());
                        UI::SameLine();
                    }

                    // ログレベルバッジ
                    ImGui::TextColored(GetMessageColor(message.level), "[%s]", GetLevelString(message.level));
                    UI::SameLine();

                    // カテゴリバッジ（Allタブのみ表示）
                    if (showCategoryBadge && !message.category.empty()) {
                        ImGui::TextColored(GetCategoryColor(message.category), "[%s]", message.category.c_str());
                        UI::SameLine();
                    }

                    // メッセージ内容
                    ImGui::TextUnformatted(message.message.c_str());
                }
            }
            clipper.End();

            // 自動スクロール
            if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }

        UI::Separator();

        // === コマンド入力 ===
        ImGui::Text("Command:");
        UI::SameLine();

        if (focusInput_) {
            ImGui::SetKeyboardFocusHere();
            focusInput_ = false;
        }

        constexpr ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
        bool enterPressed = UI::InputText("##CommandInput", inputBuffer_, sizeof(inputBuffer_),
                                           inputFlags, &ConsoleUI::InputTextCallback, this);

        UI::SameLine();
        if (ImGui::Button("Send") || enterPressed) {
            if (strlen(inputBuffer_) > 0) {
                std::string command(inputBuffer_);
                ProcessCommand(command);
                inputBuffer_[0] = '\0';
                focusInput_ = true;
            }
        }
    }
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
    tabCountsDirty_    = true;
    filteredViewDirty_ = true;
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
        case ConsoleLogLevel::Info:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);     // 白
        case ConsoleLogLevel::Warning:
            return ImVec4(1.0f, 0.8f, 0.0f, 1.0f);     // 黄色
        case ConsoleLogLevel::Error:
            return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);     // 赤
        case ConsoleLogLevel::Debug:
            return ImVec4(0.5f, 1.0f, 0.5f, 1.0f);     // 緑
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);     // 白
    }
}

const char* ConsoleUI::GetLevelString(ConsoleLogLevel level) const
{
    switch (level) {
        case ConsoleLogLevel::Info:
            return "INFO";
        case ConsoleLogLevel::Warning:
            return "WARN";
        case ConsoleLogLevel::Error:
            return "ERROR";
        case ConsoleLogLevel::Debug:
            return "DEBUG";
        default:
            return "UNKNOWN";
    }
}

std::string ConsoleUI::FormatTimestamp(const std::chrono::system_clock::time_point& timestamp) const
{
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    
    // C++20のsafe localtime
    std::tm tm;
    localtime_s(&tm, &time_t); // MSVCの安全な関数を使用
    
    ss << std::put_time(&tm, "%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

bool ConsoleUI::ShouldShowMessage(const ConsoleMessage& message) const
{
    switch (message.level) {
        case ConsoleLogLevel::Info:
            return showInfo_;
        case ConsoleLogLevel::Warning:
            return showWarning_;
        case ConsoleLogLevel::Error:
            return showError_;
        case ConsoleLogLevel::Debug:
            return showDebug_;
        default:
            return true;
    }
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
    auto particleSystem = engine_->GetService<ParticleSystem>();

    AddLog("グラフィックスシステム: " + std::string(graphicsCore ? "初期化済み" : "未初期化"), 
           graphicsCore ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("入力システム: " + std::string(inputManager ? "初期化済み" : "未初期化"), 
           inputManager ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("オーディオシステム: " + std::string(soundManager ? "初期化済み" : "未初期化"), 
           soundManager ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("ライティングシステム: " + std::string(lightManager ? "初期化済み" : "未初期化"), 
           lightManager ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

    AddLog("パーティクルシステム: " + std::string(particleSystem ? "初期化済み" : "未初期化"), 
           particleSystem ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);

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
    for (auto& msg : localMessages) {
        messages_.push_back(std::move(msg));
    }

    bool trimmed = false;
    while (messages_.size() > maxMessages_) {
        messages_.pop_front();
        trimmed = true;
    }

    tabCountsDirty_    = true;
    filteredViewDirty_ = true;
}

size_t ConsoleUI::CountMessages(const char* categoryFilter) const
{
    size_t count = 0;
    for (const auto& msg : messages_) {
        if (categoryFilter && msg.category != categoryFilter) continue;
        if (!ShouldShowMessage(msg)) continue;
        count++;
    }
    return count;
}

size_t ConsoleUI::CountErrorMessages(const char* categoryFilter) const
{
    size_t count = 0;
    for (const auto& msg : messages_) {
        if (categoryFilter && msg.category != categoryFilter) continue;
        if (msg.level == ConsoleLogLevel::Error) count++;
    }
    return count;
}

ImVec4 ConsoleUI::GetCategoryColor(const std::string& category) const
{
    if (category == "System")    return ImVec4(0.6f, 0.8f, 1.0f, 1.0f);  // 水色
    if (category == "Graphics")  return ImVec4(0.8f, 0.6f, 1.0f, 1.0f);  // 紫
    if (category == "Resource")  return ImVec4(0.6f, 1.0f, 0.8f, 1.0f);  // 青緑
    if (category == "Shader")    return ImVec4(1.0f, 1.0f, 0.6f, 1.0f);  // 黄
    if (category == "Audio")     return ImVec4(1.0f, 0.8f, 0.6f, 1.0f);  // オレンジ
    if (category == "Game")      return ImVec4(0.6f, 1.0f, 0.6f, 1.0f);  // 緑
    if (category == "Script")    return ImVec4(1.0f, 0.7f, 0.9f, 1.0f);  // 桃
    if (category == "General")   return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);  // 白
    if (category == "Console")   return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // グレー
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

void ConsoleUI::RebuildTabCounts()
{
    // 全タブのカウントをリセット
    for (int i = 0; i < kTabCount; ++i) {
        cachedTabCounts_[i]      = 0;
        cachedTabErrorCounts_[i] = 0;
    }

    // カテゴリ名 → タブインデックスのマップ（初回のみ構築）
    static std::unordered_map<std::string, int> categoryToIndex;
    if (categoryToIndex.empty()) {
        for (int i = 1; i < kTabCount; ++i) {
            categoryToIndex[kTabCategories[i]] = i;
        }
    }

    // シングルパスで全タブのカウントを同時に集計（旧: 9回走査）
    for (const auto& msg : messages_) {
        const bool visible = ShouldShowMessage(msg);
        const bool isError = (msg.level == ConsoleLogLevel::Error);

        // All タブ (index 0)
        if (visible) cachedTabCounts_[0]++;
        if (isError) cachedTabErrorCounts_[0]++;

        // カテゴリ別タブ
        auto it = categoryToIndex.find(msg.category);
        if (it != categoryToIndex.end()) {
            if (visible) cachedTabCounts_[it->second]++;
            if (isError) cachedTabErrorCounts_[it->second]++;
        }
    }
}

void ConsoleUI::RebuildFilteredView(const char* categoryFilter)
{
    filteredIndices_.clear();
    filteredIndices_.reserve(messages_.size());
    for (size_t i = 0; i < messages_.size(); ++i) {
        const auto& msg = messages_[i];
        if (categoryFilter && msg.category != categoryFilter) continue;
        if (!ShouldShowMessage(msg)) continue;
        if (filter_.IsActive() &&
            !filter_.PassFilter(msg.message.c_str()) &&
            !filter_.PassFilter(msg.category.c_str())) continue;
        filteredIndices_.push_back(i);
    }
}
} // namespace CoreEngine
#endif // USE_IMGUI
