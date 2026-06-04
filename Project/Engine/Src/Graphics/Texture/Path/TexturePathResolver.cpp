#include "pch.h"
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

        // まずフルパス文字列でそのまま検索する
        std::string assetPath = assetDB.FindAssetPath(filePath);
        if (!assetPath.empty()) {
            if (writeLog) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", 
                    std::format("  Resolved: '{}' -> '{}'", filePath, assetPath));
            }
            return assetPath;
        }

        // フルパス検索が失敗した場合、ファイル名部分だけで再検索する。
        // MTLなどの相対テクスチャ参照 (例: "uvChecker.png") がモデルディレクトリに
        // 結合されたパスとして渡されるケースに対応するためのフォールバック。
        std::filesystem::path fsPath(filePath);
        std::string fileName = fsPath.filename().string();
        if (!fileName.empty() && fileName != filePath) {
            assetPath = assetDB.FindAssetPath(fileName);
            if (!assetPath.empty()) {
                if (writeLog) {
                    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", 
                        std::format("  Resolved by filename fallback: '{}' -> '{}'", filePath, assetPath));
                }
                return assetPath;
            }
        }

        Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Resource, "{}", 
            std::format("  Asset not found in database, using path as-is: '{}'", filePath));

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

    std::string TexturePathResolver::GetCubemapSuffix() const
    {
        return (cubemapFaceSize_ > 0)
            ? "_cubemap_" + std::to_string(cubemapFaceSize_) + ".dds"
            : "_cubemap.dds";
    }

    std::string TexturePathResolver::GetCubemapDDSPath(const std::string& originalPath) const
    {
        // フェイスサイズが設定されている場合はサフィックスに含める(例: _cubemap_512.dds)
        // サイズが変わるとパスが変わり、旧キャッシュを自動的に無効化できる。
        const std::string suffix = GetCubemapSuffix();

        auto& assetDB = AssetDatabase::GetInstance();
        std::filesystem::path absPath = std::filesystem::absolute(originalPath);
        std::string guid = assetDB.GetGUID(absPath);

        if (!guid.empty()) {
            std::filesystem::path cachePath = assetDB.GetCachedTexturePath(guid, suffix);
            return cachePath.string();
        }

        std::filesystem::path path(originalPath);
        std::filesystem::path parentPath = path.parent_path();
        std::string fileName = path.stem().string();

        if (parentPath.empty()) {
            return fileName + suffix;
        }

        return (parentPath / (fileName + suffix)).string();
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


