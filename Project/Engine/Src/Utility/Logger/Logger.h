#pragma once
#include <Windows.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>

namespace CoreEngine
{

    /// @brief ログカテゴリ（大分類）
    enum class LogCategory {
        General,    // 一般
        Graphics,   // グラフィックス
        Audio,      // オーディオ
        Input,      // 入力
        System,     // システム
        Game,       // ゲームロジック
        Resource,   // リソース管理
        Shader,     // シェーダー
    };

    /// @brief サブカテゴリを表す型
    /// const char* と spdlog::format_string_t のオーバーロード競合を防ぐためのラッパー
    struct SubCategory
    {
        constexpr SubCategory() = default;
        constexpr explicit SubCategory(const char* name) : value(name) {}
        const char* value = "";
    };

    /// @brief ログサブカテゴリ定数
    /// LogCategory と組み合わせて、より細かいログファイルに分類する
    namespace LogSubCategory
    {
        // Graphics
        inline constexpr SubCategory Device        { "Device" };
        inline constexpr SubCategory Heap          { "Heap" };
        inline constexpr SubCategory RenderTarget  { "RenderTarget" };
        inline constexpr SubCategory Pipeline      { "Pipeline" };
        inline constexpr SubCategory Command       { "Command" };
        inline constexpr SubCategory SwapChain     { "SwapChain" };
        inline constexpr SubCategory Texture       { "Texture" };
        inline constexpr SubCategory Buffer        { "Buffer" };
        inline constexpr SubCategory Shader        { "Shader" };
        inline constexpr SubCategory Barrier       { "Barrier" };

        // System
        inline constexpr SubCategory Window        { "Window" };
        inline constexpr SubCategory Thread        { "Thread" };
        inline constexpr SubCategory Memory        { "Memory" };
        inline constexpr SubCategory File          { "File" };

        // Game
        inline constexpr SubCategory Scene         { "Scene" };
        inline constexpr SubCategory Object        { "Object" };
        inline constexpr SubCategory Component     { "Component" };
        inline constexpr SubCategory Physics       { "Physics" };

        // Resource
        inline constexpr SubCategory Mesh          { "Mesh" };
        inline constexpr SubCategory Material      { "Material" };
        inline constexpr SubCategory Animation     { "Animation" };

        // Audio
        inline constexpr SubCategory Playback      { "Playback" };
        inline constexpr SubCategory Mixer         { "Mixer" };
    } // namespace LogSubCategory

    /// @brief ログレベル（spdlogと互換性のある定義）
    enum class LogLevel {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Critical,

        // 既存コード互換のための別名
        INFO = Info,
        WARNING = Warn
    };

    /// @brief ログシステム - spdlogベースのカテゴリ別ログ管理
    class Logger {
    public:
        /// @brief ログのインスタンスを取得
        static Logger& GetInstance();

        /// @brief ログシステムの初期化
        void Initialize();

        /// @brief ログシステムの明示的終了処理（非同期スレッドを安全に停止）
        void Shutdown();

        /// @brief ログレベルを指定して出力
        /// @param message ログメッセージ
        /// @param level ログレベル
        /// @param category ログカテゴリ
        /// @param subCategory サブカテゴリ（省略時は大カテゴリのみ）
        void Log(const std::string& message, LogLevel level = LogLevel::INFO,
                 LogCategory category = LogCategory::General, SubCategory subCategory = {});

        /// @brief wstring対応ログ出力
        /// @param message ログメッセージ（wstring）
        /// @param level ログレベル
        /// @param category ログカテゴリ
        /// @param subCategory サブカテゴリ（省略時は大カテゴリのみ）
        void Log(const std::wstring& message, LogLevel level = LogLevel::INFO,
                 LogCategory category = LogCategory::General, SubCategory subCategory = {});

        /// @brief フォーマット付きログ出力（呼び出し側の手動文字列構築を削減）
        template <typename... Args>
        void Logf(LogLevel level, LogCategory category, spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            auto spd = GetLogger(category);
            if (!spd) { return; }
            spd->log(ToSpdLevel(level), fmt, std::forward<Args>(args)...);
        }

        /// @brief サブカテゴリ指定版フォーマット付きログ出力
        template <typename... Args>
        void Logf(LogLevel level, LogCategory category, SubCategory subCategory,
                  spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            auto spd = GetLogger(category, subCategory);
            if (!spd) { return; }
            spd->log(ToSpdLevel(level), fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うInfoログの簡易API（カテゴリ未指定時はGeneral）
        template <typename... Args>
        void Infof(spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Info, LogCategory::General, fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うInfoログの簡易API（カテゴリ指定版）
        template <typename... Args>
        void Infof(LogCategory category, spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Info, category, fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うInfoログの簡易API（サブカテゴリ指定版）
        template <typename... Args>
        void Infof(LogCategory category, SubCategory subCategory,
                   spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Info, category, subCategory, fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うWarnログの簡易API（カテゴリ指定版）
        template <typename... Args>
        void Warnf(LogCategory category, spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Warn, category, fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うWarnログの簡易API（サブカテゴリ指定版）
        template <typename... Args>
        void Warnf(LogCategory category, SubCategory subCategory,
                   spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Warn, category, subCategory, fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うErrorログの簡易API（カテゴリ指定版）
        template <typename... Args>
        void Errorf(LogCategory category, spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Error, category, fmt, std::forward<Args>(args)...);
        }

        /// @brief よく使うErrorログの簡易API（サブカテゴリ指定版）
        template <typename... Args>
        void Errorf(LogCategory category, SubCategory subCategory,
                    spdlog::format_string_t<Args...> fmt, Args&&... args)
        {
            Logf(LogLevel::Error, category, subCategory, fmt, std::forward<Args>(args)...);
        }

        /// @brief カテゴリ別ロガーを取得（サブカテゴリ省略時は大カテゴリのみ）
        /// @param category ログカテゴリ
        /// @param subCategory サブカテゴリ（省略可）
        /// @return spdlogロガーの共有ポインタ
        std::shared_ptr<spdlog::logger> GetLogger(LogCategory category, SubCategory subCategory = {});


        //========================================
           // 文字列変換ユーティリティ
           //========================================

         /// @brief stringからwstringへ変換
        std::wstring ConvertString(const std::string& str);

        /// @brief wstringからstringへ変換
        std::string ConvertString(const std::wstring& str);

        /// @brief コンソールUI転送用コールバックを設定
        /// @param callback ログレベルとメッセージを受け取るコールバック
        void SetConsoleCallback(std::function<void(LogLevel, const std::string&, const std::string&)> callback);

        /// @brief コンソールUI転送用コールバックを解除
        void ClearConsoleCallback();

    private:
        static const size_t kMaxLogFiles = 10; // 最大ログファイル数
        static const size_t kMaxLogFileSizeBytes = 10 * 1024 * 1024; // 1ファイルあたりの最大サイズ（10MB）
        static const size_t kAsyncQueueSize = 8192; // 非同期キューサイズ
        static const size_t kAsyncThreadCount = 1; // 非同期ワーカースレッド数
        static const size_t kMaxFrameSamples = 120; // フレーム時間サンプル数（2秒分 @ 60fps）

        // "Category" or "Category/SubCategory" をキーとするロガー管理（遅延生成）
        std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers_;
        mutable std::mutex loggerMutex_;
        bool isInitialized_ = false;
        bool isShuttingDown_ = false;

        // ビルドタイムスタンプ（Initialize 時に確定し、遅延生成ロガーでも同じ値を使う）
        std::string buildTimestamp_;

        // コンソールUI転送用コールバックSink
        spdlog::sink_ptr consoleSink_;

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

        /// @brief 古いログファイルをクリーンアップ（再帰スキャン対応）
        void CleanupOldLogFiles();

        /// @brief カテゴリ名を文字列に変換
        /// @param category ログカテゴリ
        /// @return カテゴリ名の文字列
        std::string CategoryToString(LogCategory category) const;

        /// @brief "Category/SubCategory" 形式のロガーキーを生成
        /// @param category ログカテゴリ
        /// @param subCategory サブカテゴリ
        /// @return ロガーキー文字列
        std::string BuildLoggerKey(LogCategory category, SubCategory subCategory) const;

        /// @brief ロガーを作成する（キー・ファイルパスはサブカテゴリを考慮）
        /// @param key ロガーキー（"Category" or "Category/SubCategory"）
        /// @param loggerName spdlog に登録するロガー名
        /// @param logFilePath ログファイルのフルパス
        /// @param defaultLevel 既定のログレベル
        /// @return 作成されたロガー
        std::shared_ptr<spdlog::logger> CreateLogger(
            const std::string& loggerName,
            const std::string& logFilePath,
            spdlog::level::level_enum defaultLevel);

        /// @brief カテゴリごとの既定ログレベルを返す
        static spdlog::level::level_enum GetDefaultCategoryLevel(LogCategory category);

        /// @brief エンジンのログレベルをspdlogのレベルへ変換する。
        static spdlog::level::level_enum ToSpdLevel(LogLevel level);
    };
}
