#include "pch.h"
#include "TextureLoadPlan.h"

#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <filesystem>
#include <format>

namespace CoreEngine
{
    TextureLoadPlan::PlanResult TextureLoadPlan::BuildPlan(
        const std::filesystem::path& resolvedPath,
        bool ddsGenerationEnabled,
        const TexturePathResolver& pathResolver,
        const std::function<bool(const std::filesystem::path&, const std::filesystem::path&)>& cubemapGenerator,
        TextureColorSpace colorSpace) const
    {
        // 初期状態は入力パスをそのまま読み込む計画にする。
        PlanResult plan{};
        plan.resolvedPath = resolvedPath;

        Logger& log = Logger::GetInstance();

        auto fileType = TextureImageProcessor::DetectFileType(plan.resolvedPath.wstring());
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
            std::filesystem::path adjacentFileName = plan.resolvedPath.stem();
            adjacentFileName += pathResolver.GetCubemapSuffix();
            const std::filesystem::path adjacentDDSPath = plan.resolvedPath.parent_path() / adjacentFileName;

            if (std::filesystem::exists(adjacentDDSPath)) {
                log.Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                    std::format("Loading from pre-generated cubemap DDS (adjacent to HDR): {}", log.PathToUtf8(adjacentDDSPath)));
                plan.resolvedPath = adjacentDDSPath;
                plan.isDDS = true;
                return plan;
            }

            // 優先度2: GUIDベースのキャッシュを確認
            const std::filesystem::path cubemapDDSPath = pathResolver.GetCubemapDDSPath(plan.resolvedPath);

            bool needsRegeneration = false;
            if (!std::filesystem::exists(cubemapDDSPath)) {
                needsRegeneration = true;
            } else {
                const auto sourceTime = std::filesystem::last_write_time(resolvedPath);
                const auto ddsTime = std::filesystem::last_write_time(cubemapDDSPath);

                if (sourceTime > ddsTime) {
                    log.Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                        std::format("HDR source file is newer than cubemap DDS, regenerating: {}", log.PathToUtf8(plan.resolvedPath)));
                    needsRegeneration = true;
                    std::filesystem::remove(cubemapDDSPath);
                }
            }

            if (needsRegeneration) {
                log.Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                    std::format("Generating cubemap DDS from HDR: {}", log.PathToUtf8(plan.resolvedPath)));

                if (cubemapGenerator(plan.resolvedPath, cubemapDDSPath)) {
                    plan.resolvedPath = cubemapDDSPath;
                    plan.isDDS = true;
                } else {
                    log.Logf(LogLevel::WARNING, LogCategory::Graphics, "{}",
                        std::format("Failed to generate cubemap, loading HDR as 2D texture: {}", log.PathToUtf8(plan.resolvedPath)));
                }
            } else {
                log.Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                    std::format("Loading from cubemap DDS cache: {}", log.PathToUtf8(cubemapDDSPath)));

                plan.resolvedPath = cubemapDDSPath;
                plan.isDDS = true;
            }

            return plan;
        }

        // 非DDS/非HDR入力の場合は通常DDSキャッシュを確認する。
        if (!plan.isDDS && !isHDR) {
            plan.ddsPathToGenerate = pathResolver.GetDDSCachePath(plan.resolvedPath, colorSpace);

            if (std::filesystem::exists(plan.ddsPathToGenerate)) {
                const auto sourceTime = std::filesystem::last_write_time(resolvedPath);
                const auto ddsTime = std::filesystem::last_write_time(plan.ddsPathToGenerate);

                if (sourceTime > ddsTime) {
                    log.Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                        std::format("Source file is newer than DDS cache, regenerating: {}", log.PathToUtf8(plan.resolvedPath)));
                    std::filesystem::remove(plan.ddsPathToGenerate);
                } else {
                    log.Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
                        std::format("Loading from DDS cache: {}", log.PathToUtf8(plan.ddsPathToGenerate)));

                    plan.resolvedPath = plan.ddsPathToGenerate;
                    plan.isDDS = true;

                    // キャッシュを採用したので生成要求は取り下げる。
                    // ここを残すと TextureLoadExecutor が「非空なら生成する」の判定で
                    // 毎起動 DDS を読み直して再圧縮し、同じ内容を書き戻してしまう
                    // （ヒットしているのに生成コストを払い続ける状態になる）。
                    plan.ddsPathToGenerate.clear();
                }
            }
        }

        return plan;
    }
}
