#include "pch.h"
#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <vector>
#include <algorithm>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>

//========================================
// コールバックSink - ConsoleUI転送用
//========================================

namespace
{
    /// @brief バッファリングされたログメッセージ
    struct BufferedLogEntry {
        CoreEngine::LogLevel level;
        std::string category;
        std::string message;
    };

    /// @brief コールバック関数でログを転送するspdlogカスタムSink
    /// コールバック未接続時はメッセージをバッファリングし、接続時に一括転送する
    class callback_sink_mt : public spdlog::sinks::base_sink<std::mutex> {
    public:
        using CallbackFn = std::function<void(CoreEngine::LogLevel, const std::string&, const std::string&)>;

        void set_callback(CallbackFn fn)
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback_ = std::move(fn);

            // バッファに溜まったメッセージを一括転送
            if (callback_) {
                for (const auto& entry : buffer_) {
                    callback_(entry.level, entry.category, entry.message);
                }
                buffer_.clear();
            }
        }

        void clear_callback()
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback_ = nullptr;
        }

    protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);

            // spdlogレベルをエンジンLogLevelに変換
            CoreEngine::LogLevel level = CoreEngine::LogLevel::Info;
            switch (msg.level) {
            case spdlog::level::trace:
            case spdlog::level::debug:
                level = CoreEngine::LogLevel::Debug;
                break;
            case spdlog::level::info:
                level = CoreEngine::LogLevel::Info;
                break;
            case spdlog::level::warn:
                level = CoreEngine::LogLevel::Warn;
                break;
            case spdlog::level::err:
            case spdlog::level::critical:
                level = CoreEngine::LogLevel::Error;
                break;
            default:
                break;
            }

            // カテゴリとメッセージを個別に転送
            std::string category(msg.logger_name.begin(), msg.logger_name.end());
            std::string payload(msg.payload.begin(), msg.payload.end());

            if (callback_) {
                callback_(level, category, payload);
            } else {
                // コールバック未接続時はバッファに蓄積
                if (buffer_.size() < kMaxBufferSize) {
                    buffer_.push_back({ level, std::move(category), std::move(payload) });
                }
            }
        }

        void flush_() override {}

    private:
        static constexpr size_t kMaxBufferSize = 2048;
        CallbackFn callback_;
        std::vector<BufferedLogEntry> buffer_;
        std::mutex callback_mutex_;
    };
}

//========================================
// Logger 実装
//========================================


namespace CoreEngine
{
    namespace
    {
        constexpr const char* kLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [tid:%t] [%n] [%^%l%$] %v";
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
        std::filesystem::create_directories("Cache/logs");

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

        // カレントディレクトリを確認（ログフォルダが想定外の場所に作成されるトラブルを防ぐ）
        {
            std::string cwd = std::filesystem::current_path().string();
            OutputDebugStringA(("[Logger] current working directory: " + cwd + "\n").c_str());
        }

        // 非同期ロギング用のグローバルスレッドプールを初期化する。
        if (!spdlog::thread_pool()) {
            spdlog::init_thread_pool(kAsyncQueueSize, kAsyncThreadCount);
        }

        // ログパターンを設定（時刻/スレッドID/カテゴリ/ログレベル/メッセージ）
        spdlog::set_pattern(kLogPattern);

        // 通常時は定期フラッシュ、重大ログはflush_on(err)で即時フラッシュする。
        spdlog::flush_every(std::chrono::seconds(2));

        // ビルドタイムスタンプを確定（遅延生成ロガーでも同じ値を使う）
        auto now = std::chrono::system_clock::now();
        auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
        buildTimestamp_ = std::format("{:%Y%m%d_%H%M%S}", localTime);

        // コンソールUI転送用Sinkを先行作成（コールバック未接続でもバッファリングする）
        if (!consoleSink_) {
            consoleSink_ = std::make_shared<callback_sink_mt>();
        }

        isInitialized_ = true;
    }

    void Logger::Shutdown()
    {
        std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> localLoggers;

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

        for (auto& [key, logger] : localLoggers) {
            if (logger) {
                logger->flush();
            }
        }

        // 非同期ロガーの登録解除後にスレッドプールを終了する。
        spdlog::drop_all();
        spdlog::shutdown();
    }

    void Logger::Log(const std::wstring& message, LogLevel level, LogCategory category, SubCategory subCategory)
    {
        Log(ConvertString(message), level, category, subCategory);
    }

    void Logger::Log(const std::string& message, LogLevel level, LogCategory category, SubCategory subCategory)
    {
        auto logger = GetLogger(category, subCategory);
        if (!logger) {
            return;
        }

        // LogLevelをspdlogレベルへ変換して一元的に出力する。
        logger->log(ToSpdLevel(level), message);
    }

    std::shared_ptr<spdlog::logger> Logger::GetLogger(LogCategory category, SubCategory subCategory)
    {
        std::lock_guard<std::mutex> lock(loggerMutex_);

        if (!isInitialized_ || isShuttingDown_) {
            return nullptr;
        }

        std::string key = BuildLoggerKey(category, subCategory);

        auto it = loggers_.find(key);
        if (it != loggers_.end()) {
            return it->second;
        }

        // 未生成のロガーは遅延生成する
        std::string categoryName = CategoryToString(category);
        const bool hasSub = (subCategory.value != nullptr && subCategory.value[0] != '\0');
        std::string loggerName   = hasSub ? (categoryName + "/" + subCategory.value) : categoryName;

        // ログファイルのディレクトリ・パスを構築
        std::string logDir = "Cache/logs/" + categoryName;
        if (hasSub) {
            logDir += "/";
            logDir += subCategory.value;
        }

        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        if (ec) {
            // フォルダ作成失敗時は OutputDebugString で通知（ロガー未生成なので直接出力）
            std::string msg = "[Logger] create_directories failed: " + logDir + " -> " + ec.message() + "\n";
            OutputDebugStringA(msg.c_str());
            return nullptr;
        }

        std::string fileName = (hasSub ? std::string(subCategory.value) : categoryName) + "_" + buildTimestamp_ + ".log";
        std::string logFilePath = logDir + "/" + fileName;

        auto logger = CreateLogger(loggerName, logFilePath, GetDefaultCategoryLevel(category));
        loggers_[key] = logger;
        return logger;
    }


    std::string Logger::CategoryToString(LogCategory category) const
    {
        switch (category) {
        case LogCategory::General:   return "General";
        case LogCategory::Graphics:  return "Graphics";
        case LogCategory::Audio:     return "Audio";
        case LogCategory::Input:     return "Input";
        case LogCategory::System:    return "System";
        case LogCategory::Game:      return "Game";
        case LogCategory::Resource:  return "Resource";
        case LogCategory::Shader:    return "Shader";
        default:                     return "Unknown";
        }
    }

    std::string Logger::BuildLoggerKey(LogCategory category, SubCategory subCategory) const
    {
        std::string key = CategoryToString(category);
        if (subCategory.value != nullptr && subCategory.value[0] != '\0') {
            key += "/";
            key += subCategory.value;
        }
        return key;
    }

    std::shared_ptr<spdlog::logger> Logger::CreateLogger(
        const std::string& loggerName,
        const std::string& logFilePath,
        spdlog::level::level_enum defaultLevel)
    {
        // シンクの作成（ローテーションファイル + Visual Studio出力 + コンソールUI転送）
        std::vector<spdlog::sink_ptr> sinks;
        sinks.reserve(3);

        // ファイルサイズが上限を超えたらローテーションする。
        auto rotatingFileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath,
            kMaxLogFileSizeBytes,
            kMaxLogFiles);
        sinks.push_back(rotatingFileSink);

        // VS の Output ウィンドウへの出力は行わない（ログはファイルへのみ出力する）

        // コンソールUI転送Sinkが存在すれば追加
        if (consoleSink_) {
            sinks.push_back(consoleSink_);
        }

        // ロガーを非同期モードで作成する（高頻度ログでメイン処理を止めにくくする）。
        auto logger = std::make_shared<spdlog::async_logger>(
            loggerName,
            sinks.begin(),
            sinks.end(),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);

        logger->set_level(defaultLevel);
        logger->set_pattern(kLogPattern);
        logger->flush_on(spdlog::level::err);  // エラー時は即座にフラッシュ

        return logger;
    }

    spdlog::level::level_enum Logger::GetDefaultCategoryLevel(LogCategory category)
    {
        switch (category) {
        case LogCategory::System:
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

    void Logger::SetConsoleCallback(std::function<void(LogLevel, const std::string&, const std::string&)> callback)
    {
        std::lock_guard<std::mutex> lock(loggerMutex_);

        // 初回作成
        if (!consoleSink_) {
            consoleSink_ = std::make_shared<callback_sink_mt>();
        }

        // コールバックを設定
        auto* sink = static_cast<callback_sink_mt*>(consoleSink_.get());
        sink->set_callback(std::move(callback));

        // 既存の全ロガーにSinkを追加（まだ追加されていない場合）
        for (auto& [key, logger] : loggers_) {
            if (!logger) continue;
            auto& sinks = logger->sinks();
            bool found = false;
            for (const auto& s : sinks) {
                if (s == consoleSink_) { found = true; break; }
            }
            if (!found) {
                sinks.push_back(consoleSink_);
            }
        }
    }

    void Logger::ClearConsoleCallback()
    {
        std::lock_guard<std::mutex> lock(loggerMutex_);
        if (consoleSink_) {
            auto* sink = static_cast<callback_sink_mt*>(consoleSink_.get());
            sink->clear_callback();
        }
    }

    void Logger::CleanupOldLogFiles()
    {
        if (!std::filesystem::exists("Cache/logs")) {
            return;
        }

        // Cache/logs 以下を再帰的にスキャンし、ディレクトリごとに古いログを削除する。
        // サブカテゴリ対応で "Graphics/Device" のような階層も自動的に処理される。
        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator("Cache/logs")) {
            if (!dirEntry.is_directory()) {
                continue;
            }

            std::vector<std::filesystem::directory_entry> logFiles;

            // ディレクトリ直下の .log ファイルのみ収集（再帰はしない）
            for (const auto& entry : std::filesystem::directory_iterator(dirEntry)) {
                if (entry.is_regular_file() && entry.path().extension() == ".log") {
                    logFiles.push_back(entry);
                }
            }

            // 上限以下なら何もしない
            if (logFiles.size() <= kMaxLogFiles) {
                continue;
            }

            // 更新日時が新しい順にソートして古いものを削除
            std::sort(logFiles.begin(), logFiles.end(),
                [](const auto& a, const auto& b) {
                    return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
                });

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
