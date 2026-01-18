#include "Logger.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <vector>
#include <algorithm>
#include <optional>

#include <spdlog/sinks/rotating_file_sink.h>

//========================================
// Logger 実装
//========================================


namespace CoreEngine
{
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
	// ロガーをフラッシュして終了
	for (auto& [category, logger] : loggers_) {
		if (logger) {
			logger->flush();
		}
	}

	// ロガーマップをクリア
	loggers_.clear();
}

void Logger::Initialize()
{
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

	// ログパターンを設定（時刻、ログレベル、メッセージ）
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
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

	// LogLevelをspdlogのレベルに変換して出力
	switch (level) {
	case LogLevel::INFO:
		logger->info(message);
		break;
	case LogLevel::WARNING:
		logger->warn(message);
		break;
	case LogLevel::Error:
		logger->error(message);
		break;
	case LogLevel::Critical:
		logger->critical(message);
		break;
	}
}

std::shared_ptr<spdlog::logger> Logger::GetLogger(LogCategory category)
{
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

	// シンクの作成（ファイル出力のみ）
	std::vector<spdlog::sink_ptr> sinks;

	// ファイル出力シンク（新規作成モード）
	auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, false);
	sinks.push_back(fileSink);

	// ロガーを作成
	auto logger = std::make_shared<spdlog::logger>(categoryName, sinks.begin(), sinks.end());
	logger->set_level(spdlog::level::trace); // 全レベルを出力
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

		// ファイルが5個以下なら何もしない
		if (logFiles.size() <= 5) {
			continue;
		}

		// 更新日時が新しい順にソート
		std::sort(logFiles.begin(), logFiles.end(),
			[](const auto& a, const auto& b) {
				return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
			});

		// 新しいファイルから5個を残して古いものを削除
		for (size_t i = 5; i < logFiles.size(); ++i) {
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
