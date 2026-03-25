#include "TexturePathResolver.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"

#include <format>
#include <filesystem>
#include <algorithm>

namespace CoreEngine
{
    std::string TexturePathResolver::ResolveAssetPath(const std::string& filePath, bool writeLog) const
    {
        auto& assetDB = AssetDatabase::GetInstance();
        std::string assetPath = assetDB.FindAssetPath(filePath);
        if (!assetPath.empty()) {
            if (writeLog) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", 
                    std::format("  Resolved: '{}' -> '{}'", filePath, assetPath));
            }
            return assetPath;
        }

        return ResolveFilePath(filePath);
    }

    std::string TexturePathResolver::GetDDSCachePath(const std::string& originalPath) const
    {
        auto& assetDB = AssetDatabase::GetInstance();
        std::filesystem::path absPath = std::filesystem::absolute(originalPath);
        std::string guid = assetDB.GetGUID(absPath);

        if (!guid.empty()) {
            std::filesystem::path cachePath = assetDB.GetCachedTexturePath(guid, ".dds");
            return cachePath.string();
        }

        std::filesystem::path path(originalPath);
        std::filesystem::path parentPath = path.parent_path();
        std::string fileName = path.stem().string();

        std::string result;
        if (parentPath.empty()) {
            result = fileName + ".dds";
        } else {
            result = (parentPath / (fileName + ".dds")).string();
        }

        std::replace(result.begin(), result.end(), '\\', '/');
        return result;
    }

    std::string TexturePathResolver::GetCubemapDDSPath(const std::string& originalPath) const
    {
        auto& assetDB = AssetDatabase::GetInstance();
        std::filesystem::path absPath = std::filesystem::absolute(originalPath);
        std::string guid = assetDB.GetGUID(absPath);

        if (!guid.empty()) {
            std::filesystem::path cachePath = assetDB.GetCachedTexturePath(guid, "_cubemap.dds");
            return cachePath.string();
        }

        std::filesystem::path path(originalPath);
        std::filesystem::path parentPath = path.parent_path();
        std::string fileName = path.stem().string();

        if (parentPath.empty()) {
            return fileName + "_cubemap.dds";
        }

        return (parentPath / (fileName + "_cubemap.dds")).string();
    }

    std::string TexturePathResolver::ResolveFilePath(const std::string& filePath) const
    {
        std::string normalized = filePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        if (normalized.starts_with("Application/Assets/")) {
            return normalized;
        }
        if (normalized.starts_with("Engine/Assets/")) {
            return normalized;
        }
        if (normalized.length() >= 2 && normalized[1] == ':') {
            return normalized;
        }

        return basePath_ + normalized;
    }
}


