#pragma once

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>

/// @brief コンソールメッセージのログレベル

namespace CoreEngine
{

    // 前方宣言
    class EngineSystem;

    enum class ConsoleLogLevel {
        Info,       // 情報
        Warning,    // 警告
        Error,      // エラー
        Debug       // デバッグ
    };

    /// @brief コンソールメッセージ構造体
    struct ConsoleMessage {
        std::string message;                                    // メッセージ内容
        std::string category;                                   // カテゴリ名
        ConsoleLogLevel level;                                  // ログレベル
        std::chrono::system_clock::time_point timestamp;       // タイムスタンプ
        std::string formattedTimestamp;                        // 事前計算済みタイムスタンプ文字列

        ConsoleMessage(const std::string& msg, ConsoleLogLevel lvl, const std::string& cat = "Console")
            : message(msg), category(cat), level(lvl), timestamp(std::chrono::system_clock::now()) {
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
        }
    };

    /// @brief ゲーム開発用デバッグコンソールUI
    class ConsoleUI {
    public:
        /// @brief 初期化
        void Initialize();

        /// @brief コンソールUIの描画
        void Draw();

        /// @brief エンジンシステムを設定（コマンドでエンジン情報取得用）
        /// @param engine エンジンシステムのポインタ
        void SetEngineSystem(EngineSystem* engine) { engine_ = engine; }

        /// @brief メッセージをログに追加
        /// @param message メッセージ内容
        /// @param level ログレベル
        void AddLog(const std::string& message, ConsoleLogLevel level = ConsoleLogLevel::Info);

        /// @brief カテゴリ付きメッセージをログに追加
        /// @param message メッセージ内容
        /// @param level ログレベル
        /// @param category カテゴリ名
        void AddLog(const std::string& message, ConsoleLogLevel level, const std::string& category);

        /// @brief ログをクリア
        void ClearLog();

        /// @brief コンソールの表示/非表示切り替え
        /// @param visible 表示フラグ
        void SetVisible(bool visible) { isVisible_ = visible; }

        /// @brief コンソールが表示されているかを取得
        /// @return true: 表示中, false: 非表示
        bool IsVisible() const { return isVisible_; }

        // === 便利メソッド ===

        /// @brief 情報メッセージを追加
        /// @param message メッセージ内容
        void LogInfo(const std::string& message);

        /// @brief 警告メッセージを追加
        /// @param message メッセージ内容
        void LogWarning(const std::string& message);

        /// @brief エラーメッセージを追加
        /// @param message メッセージ内容
        void LogError(const std::string& message);

        /// @brief デバッグメッセージを追加
        /// @param message メッセージ内容
        void LogDebug(const std::string& message);

    private:
        EngineSystem* engine_ = nullptr;                        // エンジンシステムへのポインタ
        bool isVisible_ = true;                                 // コンソールの表示フラグ
        std::deque<ConsoleMessage> messages_;                   // メッセージログ
        static constexpr size_t maxMessages_ = 1000;            // 最大メッセージ数

        // フィルター設定
        bool showInfo_ = true;                                  // 情報メッセージを表示
        bool showWarning_ = true;                               // 警告メッセージを表示
        bool showError_ = true;                                 // エラーメッセージを表示
        bool showDebug_ = true;                                 // デバッグメッセージを表示

        // 表示設定
        bool autoScroll_ = true;                                // 自動スクロール
        bool showTimestamp_ = true;                             // タイムスタンプ表示

        // 入力用
        char inputBuffer_[512] = "";                            // コマンド入力バッファ
        bool focusInput_ = false;                               // 入力欄にフォーカス

        // UI用の一時変数
        ImGuiTextFilter filter_;                                // テキストフィルター

        // スレッドセーフなメッセージキュー（非同期ログ用）
        std::mutex pendingMutex_;
        std::vector<ConsoleMessage> pendingMessages_;

        // カテゴリタブ
        int activeTab_ = 0;                                     // 現在のアクティブタブ

        // タブカウントキャッシュ（毎フレーム全件走査の防止）
        bool tabCountsDirty_ = true;
        size_t cachedTabCounts_[9] = {};
        size_t cachedTabErrorCounts_[9] = {};
        bool prevShowInfo_ = true;
        bool prevShowWarning_ = true;
        bool prevShowError_ = true;
        bool prevShowDebug_ = true;
        char prevFilterBuf_[256] = {};

        // ImGuiListClipper 用フィルター済みインデックスキャッシュ
        bool filteredViewDirty_ = true;
        int cachedFilterActiveTab_ = -1;
        std::vector<size_t> filteredIndices_;

    private:
        /// @brief メッセージの色を取得
        /// @param level ログレベル
        /// @return ImVec4形式の色
        ImVec4 GetMessageColor(ConsoleLogLevel level) const;

        /// @brief ログレベルの文字列を取得
        /// @param level ログレベル
        /// @return ログレベルの文字列
        const char* GetLevelString(ConsoleLogLevel level) const;

        /// @brief タイムスタンプを文字列に変換
        /// @param timestamp タイムスタンプ
        /// @return 時刻文字列
        std::string FormatTimestamp(const std::chrono::system_clock::time_point& timestamp) const;

        /// @brief メッセージがフィルターを通るかチェック
        /// @param message メッセージ
        /// @return true: 表示対象, false: 非表示
        bool ShouldShowMessage(const ConsoleMessage& message) const;

        /// @brief コマンド入力の処理
        /// @param command 入力されたコマンド
        void ProcessCommand(const std::string& command);

        /// @brief FPS情報を表示
        void ShowFPSInfo();

        /// @brief システム状態を表示
        void ShowSystemStatus();

        /// @brief 保留中のメッセージをメインキューに転送
        void FlushPendingMessages();

        /// @brief カテゴリ別のメッセージ数をカウント
        size_t CountMessages(const char* categoryFilter) const;

        /// @brief カテゴリ別のエラーメッセージ数をカウント
        size_t CountErrorMessages(const char* categoryFilter) const;

        /// @brief タブのメッセージカウントキャッシュを再構築（変化時のみ呼び出し）
        void RebuildTabCounts();

        /// @brief ImGuiListClipper 用フィルター済みインデックスを再構築（変化時のみ呼び出し）
        /// @param categoryFilter カテゴリフィルター（nullptr = All）
        void RebuildFilteredView(const char* categoryFilter);

        /// @brief カテゴリの表示色を取得
        ImVec4 GetCategoryColor(const std::string& category) const;
    };
}
#endif // USE_IMGUI
