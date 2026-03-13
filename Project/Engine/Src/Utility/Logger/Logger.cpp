#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <vector>
#include <algorithm>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>

//========================================
// Logger 実装
//========================================


namespace CoreEngine
{
    namespace
    {
        constexpr const char* kLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [tid:%t] [%n] [%^%l%$] %v";

        /// @brief カテゴリ別の既定ログレベルを返す。
        spdlog::level::level_enum GetDefaultCategoryLevel(LogCategory category)
        {
            switch (category) {
            case LogCategory::System:
                return spdlog::level::debug;
            case LogCategory::Graphics:
            case LogCategory::Resource:
            case LogCategory::Shader:
                return spdlog::level::debug;
            case LogCategory::General:
            case LogCategory::Game:
            case LogCategory::Audio:
            case LogCategory::Input:
            default:
                return spdlog::level::info;
            }
        }
    }

    spdlog::level::level_enum Logger::ToSpdLevel(LogLevel level)
    {
        switch (level) {
        case LogLevel::Trace:
            return spdlog::level::trace;
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
        case LogLevel::Critical:
            return spdlog::level::critical;
        default:
            return spdlog::level::info;
        }
    }

    Logger& Logger::GetInstance()
    {
        static Logger instance;
        return instance;
    }

    Logger::Logger()
    {
        // ログのディレクトリを用意
        std::filesystem::create_directory("logs");

        // 古いログファイルを削除
        CleanupOldLogFiles();

        // フレーム時間サンプル配列を初期化
        frameTimeSamples_.resize(kMaxFrameSamples, 0.0);
    }

    Logger::~Logger()
    {
        Shutdown();
    }

    void Logger::Initialize()
    {
        std::lock_guard<std::mutex> lock(loggerMutex_);

        if (isInitialized_) {
            return;
        }

        isShuttingDown_ = false;

        // 非同期ロギング用のグローバルスレッドプールを初期化する。
        if (!spdlog::thread_pool()) {
            spdlog::init_thread_pool(kAsyncQueueSize, kAsyncThreadCount);
        }

        // ログパターンを設定（時刻/スレッドID/カテゴリ/ログレベル/メッセージ）
        spdlog::set_pattern(kLogPattern);

        // 通常時は定期フラッシュ、重大ログはflush_on(err)で即時フラッシュする。
        spdlog::flush_every(std::chrono::seconds(2));

        // 現在の時間を取得してビルドタイムスタンプを作成
        auto now = std::chrono::system_clock::now();
        auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

        // ビルドタイムスタンプを作成
        std::string buildTimestamp = std::format("{:%Y%m%d_%H%M%S}", localTime);

        // 各カテゴリのロガーを作成（ビルドタイムスタンプ付き）
        loggers_[LogCategory::General] = CreateLogger(LogCategory::General, buildTimestamp);
        loggers_[LogCategory::Graphics] = CreateLogger(LogCategory::Graphics, buildTimestamp);
        loggers_[LogCategory::Audio] = CreateLogger(LogCategory::Audio, buildTimestamp);
        loggers_[LogCategory::Input] = CreateLogger(LogCategory::Input, buildTimestamp);
        loggers_[LogCategory::System] = CreateLogger(LogCategory::System, buildTimestamp);
        loggers_[LogCategory::Game] = CreateLogger(LogCategory::Game, buildTimestamp);
        loggers_[LogCategory::Resource] = CreateLogger(LogCategory::Resource, buildTimestamp);
        loggers_[LogCategory::Shader] = CreateLogger(LogCategory::Shader, buildTimestamp);

        isInitialized_ = true;
    }

    void Logger::Shutdown()
    {
        std::unordered_map<LogCategory, std::shared_ptr<spdlog::logger>> localLoggers;

        {
            std::lock_guard<std::mutex> lock(loggerMutex_);
            if (!isInitialized_ && isShuttingDown_) {
                return;
            }

            isShuttingDown_ = true;
            isInitialized_ = false;
            localLoggers = std::move(loggers_);
            loggers_.clear();
        }

        for (auto& [category, logger] : localLoggers) {
            if (logger) {
                logger->flush();
            }
        }

        // 非同期ロガーの登録解除後にスレッドプールを終了する。
        spdlog::drop_all();
        spdlog::shutdown();
    }

    void Logger::Log(const std::wstring& message, LogLevel level, LogCategory category)
    {
        Log(ConvertString(message), level, category);
    }

    void Logger::Log(const std::string& message, LogLevel level, LogCategory category)
    {
        auto logger = GetLogger(category);
        if (!logger) {
            return;
        }

        // LogLevelをspdlogレベルへ変換して一元的に出力する。
        logger->log(ToSpdLevel(level), message);
    }

    std::shared_ptr<spdlog::logger> Logger::GetLogger(LogCategory category)
    {
        std::lock_guard<std::mutex> lock(loggerMutex_);

        if (!isInitialized_ || isShuttingDown_) {
            return nullptr;
        }

        auto it = loggers_.find(category);
        if (it != loggers_.end()) {
            return it->second;
        }

        // カテゴリが見つからない場合はnullptrを返す
        return nullptr;
    }


    std::string Logger::CategoryToString(LogCategory category)
    {
        switch (category) {
        case LogCategory::General:   return "General";
        case LogCategory::Graphics:  return "Graphics";
        case LogCategory::Audio:     return "Audio";
        case LogCategory::Input:     return "Input";
        case LogCategory::System:    return "System";
        case LogCategory::Game:  return "Game";
        case LogCategory::Resource:  return "Resource";
        case LogCategory::Shader:    return "Shader";
        default:        return "Unknown";
        }
    }

    std::shared_ptr<spdlog::logger> Logger::CreateLogger(LogCategory category, const std::string& buildTimestamp)
    {
        std::string categoryName = CategoryToString(category);

        // ビルドタイムスタンプを含むログファイル名を作成
        std::string logDir = "logs/" + categoryName;
        std::filesystem::create_directories(logDir);

        std::string logFilePath = logDir + "/" + categoryName + "_" + buildTimestamp + ".log";

        // シンクの作成（ローテーションファイル + Visual Studio出力）
        std::vector<spdlog::sink_ptr> sinks;
        sinks.reserve(2);

        // ファイルサイズが上限を超えたらローテーションする。
        auto rotatingFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath,
            kMaxLogFileSizeBytes,
            kMaxLogFiles);
        sinks.push_back(rotatingFileSink);

        // デバッグ時の追跡性向上のため、Visual StudioのOutputウィンドウにも出力する。
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        sinks.push_back(msvcSink);

        // ロガーを非同期モードで作成する（高頻度ログでメイン処理を止めにくくする）。
        auto logger = std::make_shared<spdlog::async_logger>(
            categoryName,
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);

        // カテゴリごとの既定ログレベルを適用する。
        logger->set_level(GetDefaultCategoryLevel(category));
        logger->set_pattern(kLogPattern);
        logger->flush_on(spdlog::level::err);  // エラー時は即座にフラッシュ

        return logger;
    }

    void Logger::CleanupOldLogFiles()
    {
        if (!std::filesystem::exists("logs")) {
            return;
        }

        // 各カテゴリディレクトリごとにクリーンアップ
        std::vector<std::string> categories = {
            "General", "Graphics", "Audio", "Input", "System", "Game", "Resource", "Shader",
        };

        for (const auto& category : categories) {
            std::string logDir = "logs/" + category;
            if (!std::filesystem::exists(logDir)) {
                continue;
            }

            std::vector<std::filesystem::directory_entry> logFiles;

            // カテゴリディレクトリ内の .log ファイルを取得
            for (const auto& entry : std::filesystem::directory_iterator(logDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".log") {
                    logFiles.push_back(entry);
                }
            }

            // 上限以下なら何もしない
            if (logFiles.size() <= kMaxLogFiles) {
                continue;
            }

            // 更新日時が新しい順にソート
            std::sort(logFiles.begin(), logFiles.end(),
                [](const auto& a, const auto& b) {
                    return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
                });

            // 新しいファイルから上限個数を残して古いものを削除
            for (size_t i = kMaxLogFiles; i < logFiles.size(); ++i) {
                std::filesystem::remove(logFiles[i]);
            }
        }
    }

    std::wstring Logger::ConvertString(const std::string& str)
    {
        if (str.empty()) {
            return std::wstring();
        }

        auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
        if (sizeNeeded == 0) {
            return std::wstring();
        }
        std::wstring result(sizeNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
        return result;
    }

    std::string Logger::ConvertString(const std::wstring& str)
    {
        if (str.empty()) {
            return std::string();
        }

        auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
        if (sizeNeeded == 0) {
            return std::string();
        }
        std::string result(sizeNeeded, 0);
        WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
        return result;
    }
}
