#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <deque>
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
	ConsoleLogLevel level;                                  // ログレベル
	std::chrono::system_clock::time_point timestamp;       // タイムスタンプ
	
	ConsoleMessage(const std::string& msg, ConsoleLogLevel lvl) 
		: message(msg), level(lvl), timestamp(std::chrono::system_clock::now()) {}
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
};
}
