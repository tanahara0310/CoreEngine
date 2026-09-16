#pragma once

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>

namespace CoreEngine
{

    // 前方宣言
    class EngineSystem;

    /// @brief コンソールメッセージのログレベル
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

        std::string sourceFile;                                 // 本文に含まれるスクリプトのファイル（無ければ空）
        int sourceLine = 0;                                     // その行（1 始まり）
        int sourceColumn = 0;                                   // その桁（1 始まり）

        ConsoleMessage(const std::string& msg, ConsoleLogLevel lvl, const std::string& cat = "Console");
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

        /// @brief 次の描画でコンソールのウィンドウを前に出す
        void RequestFocus() { focusWindow_ = true; }

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
        /// @brief 画面に並べる 1 行（畳んだときは同じ内容の件数を持つ）
        struct ViewRow {
            size_t index = 0;   // messages_ の位置（畳んだときは最後に出た位置）
            size_t count = 1;   // 同じ内容の件数
        };

        EngineSystem* engine_ = nullptr;                        // エンジンシステムへのポインタ
        bool isVisible_ = true;                                 // コンソールの表示フラグ
        bool focusWindow_ = false;                              // 次の描画で前に出すか
        std::deque<ConsoleMessage> messages_;                   // メッセージログ
        static constexpr size_t maxMessages_ = 1000;            // 最大メッセージ数
        static constexpr int kLevelCount = 4;                   // ログレベルの数

        // レベルごとの表示（ConsoleLogLevel の並び）
        bool showLevel_[kLevelCount] = { true, true, true, true };

        // 表示設定
        int categoryFilter_ = 0;                                // 0 はすべて
        bool collapse_ = false;                                 // 同じ内容の行を畳む
        bool pauseOnError_ = false;                             // エラーが出たら再生を止める
        bool autoScroll_ = true;                                // 自動スクロール
        bool showTimestamp_ = false;                            // タイムスタンプ表示

        // 入力用
        char inputBuffer_[512] = "";                            // コマンド入力バッファ
        bool focusInput_ = false;                               // 入力欄にフォーカス
        std::vector<std::string> history_;                      // 送ったコマンド（古い順）
        int historyPos_ = -1;                                   // 履歴をたどっている位置（-1 はたどっていない）

        // UI用の一時変数
        ImGuiTextFilter filter_;                                // テキストフィルター

        // スレッドセーフなメッセージキュー（非同期ログ用）
        std::mutex pendingMutex_;
        std::vector<ConsoleMessage> pendingMessages_;

        // 件数と表示する行の控え（変化したときだけ作り直す）
        bool viewDirty_ = true;
        size_t levelCounts_[kLevelCount] = {};
        std::vector<ViewRow> viewRows_;
        bool prevShowLevel_[kLevelCount] = { true, true, true, true };
        int prevCategoryFilter_ = 0;
        bool prevCollapse_ = false;
        char prevFilterBuf_[256] = {};

    private:
        /// @brief メッセージの色を取得
        ImVec4 GetMessageColor(ConsoleLogLevel level) const;

        /// @brief ログレベルの表示名を取得
        const char* GetLevelString(ConsoleLogLevel level) const;

        /// @brief メッセージがレベルとカテゴリの絞り込みを通るか
        bool ShouldShowMessage(const ConsoleMessage& message) const;

        /// @brief 件数・レベルの切り替え・クリア・畳む・一時停止・検索の行
        void DrawToolbar();

        /// @brief ログの一覧
        void DrawRows();

        /// @brief コマンドの入力欄
        void DrawCommandInput();

        /// @brief メッセージに含まれるスクリプトの行を外部エディタで開く
        void OpenSource(const ConsoleMessage& message) const;

        /// @brief コマンド入力の処理
        /// @param command 入力されたコマンド
        void ProcessCommand(const std::string& command);

        /// @brief 入力欄のコールバック（↑ ↓ で履歴、Tab で補完）
        static int InputTextCallback(ImGuiInputTextCallbackData* data);

        /// @brief InputTextCallback の本体
        int OnInputText(ImGuiInputTextCallbackData& data);

        /// @brief 入力の続きとして使える候補（コマンド名と CVar の名前）
        std::vector<std::string> CollectCompletions(const char* text) const;

        /// @brief FPS情報を表示
        void ShowFPSInfo();

        /// @brief システム状態を表示
        void ShowSystemStatus();

        /// @brief 保留中のメッセージをメインキューに転送
        void FlushPendingMessages();

        /// @brief レベルごとの件数と表示する行を作り直す（変化したときだけ呼ぶ）
        void RebuildView();
    };
}
#endif // USE_IMGUI
