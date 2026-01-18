#include "ConsoleUI.h"
#include "Engine/EngineSystem/EngineSystem.h"

// コンポーネントのインクルード
#include "Utility/FrameRate/FrameRateController.h"

#include <iomanip>
#include <sstream>
#include <algorithm>


namespace CoreEngine
{
void ConsoleUI::Initialize()
{
    // 初期メッセージを追加
    AddLog("コンソールが初期化されました", ConsoleLogLevel::Info);
    AddLog("デバッグコンソールが使用可能です", ConsoleLogLevel::Debug);
}

void ConsoleUI::Draw()
{
    if (!isVisible_) return;

    if (ImGui::Begin("Console")) {
        // === コンソールヘッダー ===
        ImGui::Text("Debug Console");
        ImGui::SameLine();
        
        // クリアボタン
        if (ImGui::Button("Clear")) {
            ClearLog();
        }
        
        ImGui::SameLine();
        
        // フィルター設定
        if (ImGui::Button("Settings")) {
            ImGui::OpenPopup("ConsoleSettings");
        }
        
        // 設定ポップアップ
        if (ImGui::BeginPopup("ConsoleSettings")) {
            ImGui::Text("Display Settings");
            ImGui::Separator();
            ImGui::Checkbox("Auto Scroll", &autoScroll_);
            ImGui::Checkbox("Show Timestamp", &showTimestamp_);
            
            ImGui::Separator();
            ImGui::Text("Log Level Filter");
            ImGui::Checkbox("Info", &showInfo_);
            ImGui::SameLine();
            ImGui::Checkbox("Warning", &showWarning_);
            ImGui::SameLine();
            ImGui::Checkbox("Error", &showError_);
            ImGui::SameLine();
            ImGui::Checkbox("Debug", &showDebug_);
            
            ImGui::EndPopup();
        }
        
        ImGui::Separator();
        
        // === テキストフィルター ===
        ImGui::Text("Filter:");
        ImGui::SameLine();
        filter_.Draw("##Filter", -100.0f);
        ImGui::SameLine();
        if (ImGui::Button("X")) {
            filter_.Clear();
        }
        
        ImGui::Separator();
        
        // === メッセージ表示エリア ===
        const float footerHeight = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeight), false, ImGuiWindowFlags_HorizontalScrollbar)) {
            
            // メッセージ表示
            for (const auto& message : messages_) {
                if (!ShouldShowMessage(message)) continue;
                if (!filter_.PassFilter(message.message.c_str())) continue;
                
                // タイムスタンプ表示
                if (showTimestamp_) {
                    std::string timeStr = FormatTimestamp(message.timestamp);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[%s]", timeStr.c_str());
                    ImGui::SameLine();
                }
                
                // ログレベル表示
                const char* levelStr = GetLevelString(message.level);
                ImVec4 levelColor = GetMessageColor(message.level);
                ImGui::TextColored(levelColor, "[%s]", levelStr);
                ImGui::SameLine();
                
                // メッセージ内容表示
                ImGui::TextWrapped("%s", message.message.c_str());
            }
            
            // 自動スクロール
            if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // === コマンド入力エリア ===
        ImGui::Text("Command:");
        ImGui::SameLine();
        
        // 入力フォーカス設定
        if (focusInput_) {
            ImGui::SetKeyboardFocusHere();
            focusInput_ = false;
        }
        
        // コマンド入力
        bool enterPressed = ImGui::InputText("##CommandInput", inputBuffer_, sizeof(inputBuffer_), 
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        
        ImGui::SameLine();
        if (ImGui::Button("Send") || enterPressed) {
            if (strlen(inputBuffer_) > 0) {
                std::string command(inputBuffer_);
                ProcessCommand(command);
                inputBuffer_[0] = '\0'; // バッファをクリア
                focusInput_ = true; // 次フレームで入力にフォーカス
            }
        }
    }
    ImGui::End();
}

void ConsoleUI::AddLog(const std::string& message, ConsoleLogLevel level)
{
    // メッセージを追加
    messages_.emplace_back(message, level);
    
    // 最大数を超えたら古いメッセージを削除
    if (messages_.size() > maxMessages_) {
        messages_.pop_front();
    }
}

void ConsoleUI::ClearLog()
{
    messages_.clear();
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
    // === 不明なコマンド ===
    else {
        AddLog("不明なコマンド: " + cmd + " (help で利用可能なコマンドを表示)", ConsoleLogLevel::Error);
    }
}

void ConsoleUI::ShowFPSInfo()
{
    if (!engine_) {
        AddLog("エラー: エンジンシステムが設定されていません", ConsoleLogLevel::Error);
        return;
    }

    // 【Phase 4】新方式でコンポーネントを取得
    auto frameRate = engine_->GetComponent<FrameRateController>();
    if (!frameRate) {
        AddLog("エラー: フレームレートコントローラーが利用できません", ConsoleLogLevel::Error);
        return;
    }

    AddLog("=== FPS情報 ===", ConsoleLogLevel::Info);
    
    float currentFPS = frameRate->GetCurrentFPS();
    float targetFPS = frameRate->GetTargetFPS();
    float deltaTime = frameRate->GetDeltaTime() * 1000.0f; // ms
    
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
    auto directXCommon = engine_->GetComponent<DirectXCommon>();
    auto inputManager = engine_->GetComponent<InputManager>();
    auto soundManager = engine_->GetComponent<SoundManager>();
    auto lightManager = engine_->GetComponent<LightManager>();
    auto particleSystem = engine_->GetComponent<ParticleSystem>();
    
    AddLog("グラフィックスシステム: " + std::string(directXCommon ? "初期化済み" : "未初期化"), 
           directXCommon ? ConsoleLogLevel::Info : ConsoleLogLevel::Error);
    
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
}
