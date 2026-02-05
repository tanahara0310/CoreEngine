#pragma once
#include <Windows.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h>

/// @brief ログカテゴリ

namespace CoreEngine
{
enum class LogCategory {
    General,    // 一般
    Graphics,   // グラフィックス
    Audio,      // オーディオ
    Input,      // 入力
    System,  // システム
    Game,       // ゲームロジック
    Resource,   // リソース管理
    Shader,     // シェーダー
};

/// @brief ログレベル（spdlogと互換性のある定義）
enum class LogLevel {
    INFO,
    WARNING,
    Error,
    Critical
};

/// @brief ログシステム - spdlogベースのカテゴリ別ログ管理
class Logger {
public:
    /// @brief ログのインスタンスを取得
    static Logger& GetInstance();

    /// @brief ログシステムの初期化
    void Initialize();

    /// @brief ログレベルを指定して出力
    /// @param message ログメッセージ
    /// @param level ログレベル
    /// @param category ログカテゴリ
    void Log(const std::string& message, LogLevel level = LogLevel::INFO, LogCategory category = LogCategory::General);

    /// @brief wstring対応ログ出力
    /// @param message ログメッセージ（wstring）
    /// @param level ログレベル
    /// @param category ログカテゴリ
    void Log(const std::wstring& message, LogLevel level = LogLevel::INFO, LogCategory category = LogCategory::General);

    /// @brief カテゴリ別ロガーを取得
    /// @param category ログカテゴリ
    /// @return spdlogロガーの共有ポインタ
    std::shared_ptr<spdlog::logger> GetLogger(LogCategory category);


    //========================================
       // 文字列変換ユーティリティ
       //========================================

     /// @brief stringからwstringへ変換
    std::wstring ConvertString(const std::string& str);

    /// @brief wstringからstringへ変換
    std::string ConvertString(const std::wstring& str);

private:
    static const size_t kMaxLogFiles = 10; // 最大ログファイル数
    static const size_t kMaxFrameSamples = 120; // フレーム時間サンプル数（2秒分 @ 60fps）

    // カテゴリ別のロガー管理
    std::unordered_map<LogCategory, std::shared_ptr<spdlog::logger>> loggers_;

    // フレーム時間管理
    mutable std::mutex frameTimeMutex_;
    std::vector<double> frameTimeSamples_;
    size_t frameTimeSampleIndex_ = 0;

    // ログのコンストラクタ・デストラクタ
    Logger();
    ~Logger();

    // コピーコンストラクタ・代入演算子は削除
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// @brief 古いログファイルをクリーンアップ
    void CleanupOldLogFiles();

    /// @brief カテゴリ名を文字列に変換
    /// @param category ログカテゴリ
    /// @return カテゴリ名の文字列
    std::string CategoryToString(LogCategory category);

    /// @brief カテゴリ別ロガーを作成
    /// @param category ログカテゴリ
    /// @param buildTimestamp ビルドのタイムスタンプ
  /// @return 作成されたロガー
    std::shared_ptr<spdlog::logger> CreateLogger(LogCategory category, const std::string& buildTimestamp);
};
}
