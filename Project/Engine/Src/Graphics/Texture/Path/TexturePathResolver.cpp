#include "pch.h"
#include "TexturePathResolver.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"

#include <format>
#include <filesystem>
#include <algorithm>

namespace CoreEngine
{
    std::filesystem::path TexturePathResolver::ResolveAssetPath(const std::string& filePath, bool writeLog) const
    {
        auto& assetDB = AssetDatabase::GetInstance();

        // まずフルパス文字列でそのまま検索する
        std::filesystem::path assetPath = assetDB.FindAssetPath(filePath);
        if (!assetPath.empty()) {
            if (writeLog) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}",
                    std::format("  Resolved: '{}' -> '{}'", filePath, Logger::GetInstance().PathToUtf8(assetPath)));
            }
            return assetPath;
        }

        // フルパス検索が失敗した場合、ファイル名部分だけで再検索する。
        // MTLなどの相対テクスチャ参照 (例: "uvChecker.png") がモデルディレクトリに
        // 結合されたパスとして渡されるケースに対応するためのフォールバック。
        std::filesystem::path fsPath = Logger::GetInstance().Utf8ToPath(filePath);
        std::string fileName = Logger::GetInstance().PathToUtf8(fsPath.filename());
        if (!fileName.empty() && fileName != filePath) {
            assetPath = assetDB.FindAssetPath(fileName);
            if (!assetPath.empty()) {
                if (writeLog) {
                    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}",
                        std::format("  Resolved by filename fallback: '{}' -> '{}'", filePath, Logger::GetInstance().PathToUtf8(assetPath)));
                }
                return assetPath;
            }
        }

        Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Resource, "{}",
            std::format("  Asset not found in database, using path as-is: '{}'", filePath));

        return ResolveFilePath(filePath);
    }

    std::filesystem::path TexturePathResolver::GetDDSCachePath(const std::filesystem::path& originalPath,
        TextureColorSpace colorSpace) const
    {
        // 色空間ごとにキャッシュを分離する（同一ソースでも sRGB / Linear で内容が異なるため）
        const std::string suffix = (colorSpace == TextureColorSpace::Linear)
            ? "_linear.dds"
            : ".dds";

        auto& assetDB = AssetDatabase::GetInstance();
        std::filesystem::path absPath = std::filesystem::absolute(originalPath);
        std::string guid = assetDB.GetGUID(absPath);

        if (!guid.empty()) {
            return assetDB.GetCachedTexturePath(guid, suffix);
        }

        // GUID が無い場合は元ファイルの隣に置く。stem はパスの一部なので
        // narrow 文字列に落とさず path の連結で組み立てる。
        std::filesystem::path fileName = originalPath.stem();
        fileName += suffix;
        return originalPath.parent_path() / fileName;
    }

    std::string TexturePathResolver::GetCubemapSuffix() const
    {
        return (cubemapFaceSize_ > 0)
            ? "_cubemap_" + std::to_string(cubemapFaceSize_) + ".dds"
            : "_cubemap.dds";
    }

    std::filesystem::path TexturePathResolver::GetCubemapDDSPath(const std::filesystem::path& originalPath) const
    {
        // フェイスサイズが設定されている場合はサフィックスに含める(例: _cubemap_512.dds)
        // サイズが変わるとパスが変わり、旧キャッシュを自動的に無効化できる。
        const std::string suffix = GetCubemapSuffix();

        auto& assetDB = AssetDatabase::GetInstance();
        std::filesystem::path absPath = std::filesystem::absolute(originalPath);
        std::string guid = assetDB.GetGUID(absPath);

        if (!guid.empty()) {
            return assetDB.GetCachedTexturePath(guid, suffix);
        }

        std::filesystem::path fileName = originalPath.stem();
        fileName += suffix;
        return originalPath.parent_path() / fileName;
    }

    std::filesystem::path TexturePathResolver::ResolveFilePath(const std::string& filePath) const
    {
        // 入力は AssetDatabase に登録が無かった UTF-8 の要求文字列。
        // 区切りだけ正規化して path へ持ち上げ、以降はエンコーディングを意識しない。
        std::string normalized = filePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        if (!normalized.starts_with("Application/Assets/") &&
            !normalized.starts_with("Engine/Assets/") &&
            !(normalized.length() >= 2 && normalized[1] == ':')) {
            normalized = basePath_ + normalized;
        }

        return Logger::GetInstance().Utf8ToPath(normalized);
    }
}
