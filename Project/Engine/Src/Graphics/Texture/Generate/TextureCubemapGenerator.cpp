#include "TextureCubemapGenerator.h"
#include "Utility/Logger/Logger.h"
#include "Utility/ProcessExecutor/ProcessExecutor.h"

#include <format>
#include <algorithm>
#include <thread>
#include <chrono>

namespace CoreEngine
{
    bool TextureCubemapGenerator::GenerateFromHDR(const std::string& hdrPath, const std::string& cubemapDDSPath) const
    {
        try {
            // まずcmft実行ファイルを確認し、見つからない場合は早期終了する。
            std::filesystem::path cmftPath = FindCmftExecutable();
            if (cmftPath.empty()) {
                Logger::GetInstance().Log("cmft executable not found. Please build cmft first.", LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            // 実行時の解釈違いを防ぐため、すべてのパスを絶対化してからコマンドを構築する。
            std::filesystem::path hdrPathAbs = std::filesystem::absolute(hdrPath);
            std::filesystem::path outputPathAbs = std::filesystem::absolute(cubemapDDSPath);
            std::filesystem::path outputBase = outputPathAbs.parent_path() / outputPathAbs.stem();
            std::filesystem::path cmftPathAbs = std::filesystem::absolute(cmftPath);

            std::string cmftPathStr = ConvertToUnixPath(cmftPathAbs);
            std::string hdrPathStr = ConvertToUnixPath(hdrPathAbs);
            std::string outputBaseStr = ConvertToUnixPath(outputBase);

            Logger::GetInstance().Log(std::format("cmft path: {}", cmftPathStr), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("HDR input: {}", hdrPathStr), LogLevel::INFO, LogCategory::Graphics);
            Logger::GetInstance().Log(std::format("Output base: {}", outputBaseStr), LogLevel::INFO, LogCategory::Graphics);

            std::string command = std::format(
                "\"{}\" --input \"{}\" --output0 \"{}\" --output0params dds,rgba16f,cubemap",
                cmftPathStr,
                hdrPathStr,
                outputBaseStr);

            Logger::GetInstance().Log(std::format("Executing: {}", command), LogLevel::INFO, LogCategory::Graphics);

            // 外部プロセスで変換を実行し、失敗時は詳細をログ出力する。
            auto result = ProcessExecutor::Execute(command);
            if (!result.success) {
                Logger::GetInstance().Log(std::format("Failed to execute cmft: GetLastError={}", result.lastError), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            // 変換直後のファイル反映待ちを入れてから検証する。
            std::this_thread::sleep_for(std::chrono::milliseconds(processWaitTimeOutMs_));
            bool isValid = ValidateGeneratedCubemap(cubemapDDSPath);

            Logger::GetInstance().Log(std::format("cmft exit code: {}, validation: {}", result.exitCode, isValid ? "OK" : "FAILED"), LogLevel::INFO, LogCategory::Graphics);
            return isValid;
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log(std::format("Exception during cubemap generation: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }

    std::filesystem::path TextureCubemapGenerator::FindCmftExecutable() const
    {
        // 既定相対パスの存在確認のみを行い、探索コストを抑える。
        std::filesystem::path cmftPath = cmtfRelativePath_;
        if (std::filesystem::exists(cmftPath)) {
            return cmftPath;
        }

        return std::filesystem::path();
    }

    std::string TextureCubemapGenerator::ConvertToUnixPath(const std::filesystem::path& path)
    {
        // Windows区切りをcmft互換のスラッシュ区切りへ統一する。
        std::string str = path.string();
        std::replace(str.begin(), str.end(), '\\', '/');
        return str;
    }

    bool TextureCubemapGenerator::ValidateGeneratedCubemap(const std::string& filePath) const
    {
        // 生成ファイルの存在とサイズの最小要件を確認して結果を返す。
        if (!std::filesystem::exists(filePath)) {
            return false;
        }

        try {
            auto fileSize = std::filesystem::file_size(filePath);
            if (fileSize == 0) {
                Logger::GetInstance().Log(std::format("Cubemap file is empty: {}", filePath), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            Logger::GetInstance().Log(std::format("Cubemap DDS generated: {} (size: {} bytes)", filePath, fileSize), LogLevel::INFO, LogCategory::Graphics);
            return true;
        }
        catch (const std::filesystem::filesystem_error& e) {
            Logger::GetInstance().Log(std::format("Failed to validate cubemap file: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }
}
