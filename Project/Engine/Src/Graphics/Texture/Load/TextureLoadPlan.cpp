#include "TextureLoadPlan.h"

#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <filesystem>
#include <format>

namespace CoreEngine
{
    TextureLoadPlan::PlanResult TextureLoadPlan::BuildPlan(
        const std::string& resolvedPath,
        bool ddsGenerationEnabled,
        const TexturePathResolver& pathResolver,
        const std::function<bool(const std::string&, const std::string&)>& cubemapGenerator) const
    {
        // 初期状態は入力パスをそのまま読み込む計画にする。
        PlanResult plan{};
        plan.resolvedPath = resolvedPath;

        std::wstring currentPathW = Logger::GetInstance().ConvertString(plan.resolvedPath);
        auto fileType = TextureImageProcessor::DetectFileType(currentPathW);
        plan.isDDS = (fileType == TextureImageProcessor::FileType::DDS);
        const bool isHDR = (fileType == TextureImageProcessor::FileType::HDR);

        // DDS生成無効時は変換や再生成を行わず、そのまま返す。
        if (!ddsGenerationEnabled) {
            return plan;
        }

        // HDR入力の場合はCubemapDDSキャッシュ利用を優先する。
        if (isHDR) {
            // 優先度1:HDRファイルと同じディレクトリにある事前生成済みDDSを確認
            // （排出ビルドでデプロイする場合に対応）
            const std::string suffix = pathResolver.GetCubemapSuffix();
            std::filesystem::path hdrFsPath(plan.resolvedPath);
            std::string adjacentDDSPath = (hdrFsPath.parent_path() / (hdrFsPath.stem().string() + suffix)).string();
            const std::wstring adjacentDDSPathW = Logger::GetInstance().ConvertString(adjacentDDSPath);

            if (std::filesystem::exists(adjacentDDSPathW)) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", 
                    std::format("Loading from pre-generated cubemap DDS (adjacent to HDR): {}", adjacentDDSPath));
                plan.resolvedPath = adjacentDDSPath;
                plan.isDDS = true;
                return plan;
            }

            // 優先度2: GUIDベースのキャッシュを確認
            const std::string cubemapDDSPath = pathResolver.GetCubemapDDSPath(plan.resolvedPath);
            const std::wstring cubemapDDSPathW = Logger::GetInstance().ConvertString(cubemapDDSPath);

            bool needsRegeneration = false;
            if (!std::filesystem::exists(cubemapDDSPathW)) {
                needsRegeneration = true;
            } else {
                const auto sourceTime = std::filesystem::last_write_time(currentPathW);
                const auto ddsTime = std::filesystem::last_write_time(cubemapDDSPathW);

                if (sourceTime > ddsTime) {
                    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", 
                        std::format("HDR source file is newer than cubemap DDS, regenerating: {}", plan.resolvedPath));
                    needsRegeneration = true;
                    std::filesystem::remove(cubemapDDSPathW);
                }
            }

            if (needsRegeneration) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", 
                    std::format("Generating cubemap DDS from HDR: {}", plan.resolvedPath));

                if (cubemapGenerator(plan.resolvedPath, cubemapDDSPath)) {
                    plan.resolvedPath = cubemapDDSPath;
                    plan.isDDS = true;
                } else {
                    Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", 
                        std::format("Failed to generate cubemap, loading HDR as 2D texture: {}", plan.resolvedPath));
                }
            } else {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", 
                    std::format("Loading from cubemap DDS cache: {}", cubemapDDSPath));

                plan.resolvedPath = cubemapDDSPath;
                plan.isDDS = true;
            }

            return plan;
        }

        // 非DDS/非HDR入力の場合は通常DDSキャッシュを確認する。
        if (!plan.isDDS && !isHDR) {
            plan.ddsPathToGenerate = pathResolver.GetDDSCachePath(plan.resolvedPath);
            const std::wstring ddsPathW = Logger::GetInstance().ConvertString(plan.ddsPathToGenerate);

            if (std::filesystem::exists(ddsPathW)) {
                const auto sourceTime = std::filesystem::last_write_time(currentPathW);
                const auto ddsTime = std::filesystem::last_write_time(ddsPathW);

                if (sourceTime > ddsTime) {
                    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", 
                        std::format("Source file is newer than DDS cache, regenerating: {}", plan.resolvedPath));
                    std::filesystem::remove(ddsPathW);
                } else {
                    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", 
                        std::format("Loading from DDS cache: {}", plan.ddsPathToGenerate));

                    plan.resolvedPath = plan.ddsPathToGenerate;
                    plan.isDDS = true;
                }
            }
        }

        return plan;
    }
}


