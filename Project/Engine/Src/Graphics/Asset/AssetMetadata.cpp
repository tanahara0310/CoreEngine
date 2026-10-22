#include "pch.h"
#include "AssetMetadata.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include <sstream>
#include <random>
#include <iomanip>
#include <chrono>

namespace CoreEngine
{
    std::string AssetMetadata::LoadOrCreateMetaFile(const std::filesystem::path& assetPath, AssetType type)
    {
        std::filesystem::path metaPath = GetMetaFilePath(assetPath);

        // メタファイルが存在する場合は読み込み
        if (std::filesystem::exists(metaPath))
        {
            std::string guid;
            AssetType loadedType;
            if (LoadFromJSON(metaPath, guid, loadedType))
            {
                return guid;
            }

            // 読めないメタファイルは上書きせず、GUID なしとして返す
            Logger& log = Logger::GetInstance();
            log.Logf(LogLevel::Error, LogCategory::System, "{}",
                "Failed to read meta file (GUID is kept unchanged): " + log.PathToUtf8(metaPath));
            return "";
        }

        // 存在しない場合は新規作成
        std::string newGuid = GenerateGUID();
        SaveMetaFile(assetPath, newGuid, type);

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::System, "{}", 
            "Created meta file for: " + assetPath.filename().string() + " (GUID: " + newGuid + ")");

        return newGuid;
    }

    std::string AssetMetadata::LoadGUID(const std::filesystem::path& metaFilePath)
    {
        std::string guid;
        AssetType type;
        if (LoadFromJSON(metaFilePath, guid, type))
        {
            return guid;
        }
        return "";
    }

    AssetMetadata::ImportSettings AssetMetadata::LoadImportSettings(const std::filesystem::path& metaFilePath)
    {
        ImportSettings settings;
        Logger& log = Logger::GetInstance();
        const json data = JsonManager::GetInstance().LoadJson(log.PathToUtf8(metaFilePath));
        if (!data.is_object()) {
            return settings;
        }
        const auto import = data.find("import");
        if (import == data.end() || !import->is_object()) {
            return settings;
        }

        // 決まった値だけを受け取り、ほかの値は警告して読み飛ばす
        const auto read = [&](const char* key, std::initializer_list<const char*> allowed, const char* hint) {
            const std::string value = JsonManager::SafeGet<std::string>(*import, key, "");
            for (const char* candidate : allowed) {
                if (value == candidate) {
                    return value;
                }
            }
            if (!value.empty()) {
                log.Logf(LogLevel::Warn, LogCategory::System, "{} の import.{} \"{}\" は {} で書きます（読み飛ばしました）",
                    log.PathToUtf8(metaFilePath), key, value, hint);
            }
            return std::string();
        };
        settings.colorSpace = read("colorSpace", { "srgb", "linear" }, "\"srgb\" か \"linear\"");
        settings.format = read("format", { "bc7", "bc3", "none" }, "\"bc7\"・\"bc3\"・\"none\" のどれか");
        return settings;
    }

    bool AssetMetadata::SaveImportSettings(const std::filesystem::path& metaFilePath, const ImportSettings& settings)
    {
        Logger& log = Logger::GetInstance();
        const std::string metaUtf8 = log.PathToUtf8(metaFilePath);
        json data = JsonManager::GetInstance().LoadJson(metaUtf8);
        if (!data.is_object() || JsonManager::SafeGet<std::string>(data, "guid", "").empty()) {
            log.Logf(LogLevel::Error, LogCategory::System, "取り込み設定を書く .meta が読めません: {}", metaUtf8);
            return false;
        }

        // ほかの項目は残し、色空間と形式だけを書き換える（空なら消す）
        json import = data.value("import", json::object());
        if (!import.is_object()) {
            import = json::object();
        }
        const auto assign = [&import](const char* key, const std::string& value) {
            if (value.empty()) {
                import.erase(key);
            } else {
                import[key] = value;
            }
        };
        assign("colorSpace", settings.colorSpace);
        assign("format", settings.format);
        if (import.empty()) {
            data.erase("import");
        } else {
            data["import"] = std::move(import);
        }
        if (!JsonManager::GetInstance().SaveJson(metaUtf8, data)) {
            log.Logf(LogLevel::Error, LogCategory::System, "取り込み設定を .meta に書けません: {}", metaUtf8);
            return false;
        }
        return true;
    }

    // アセット本体の隣に .meta を書く（Unity と同じ流儀）。GUID は 1 度作ったら変えない
    void AssetMetadata::SaveMetaFile(const std::filesystem::path& assetPath, const std::string& guid, AssetType type)
    {
        std::filesystem::path metaPath = GetMetaFilePath(assetPath);
        SaveAsJSON(metaPath, guid, type);
    }

    void AssetMetadata::DeleteMetaFile(const std::filesystem::path& assetPath)
    {
        std::filesystem::path metaPath = GetMetaFilePath(assetPath);
        if (std::filesystem::exists(metaPath))
        {
            std::filesystem::remove(metaPath);
        }
    }

    std::filesystem::path AssetMetadata::GetMetaFilePath(const std::filesystem::path& assetPath)
    {
        return assetPath.string() + ".meta";
    }

    std::string AssetMetadata::GenerateGUID()
    {
        // 簡易的なGUID生成（UUID v4形式）
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        uint64_t data1 = dis(gen);
        uint64_t data2 = dis(gen);

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(8) << ((data1 >> 32) & 0xFFFFFFFF) << "-";
        ss << std::setw(4) << ((data1 >> 16) & 0xFFFF) << "-";
        ss << std::setw(4) << (data1 & 0xFFFF) << "-";
        ss << std::setw(4) << ((data2 >> 48) & 0xFFFF) << "-";
        ss << std::setw(12) << (data2 & 0xFFFFFFFFFFFF);

        return ss.str();
    }

    void AssetMetadata::SaveAsJSON(const std::filesystem::path& metaFilePath, const std::string& guid, AssetType type)
    {
        const json data = {
            { "guid", guid },
            { "type", AssetTypeToString(type) }
        };

        Logger& log = Logger::GetInstance();
        if (!JsonManager::GetInstance().SaveJson(log.PathToUtf8(metaFilePath), data))
        {
            log.Logf(LogLevel::Error, LogCategory::System, "{}",
                "Failed to create meta file: " + log.PathToUtf8(metaFilePath));
        }
    }

    bool AssetMetadata::LoadFromJSON(const std::filesystem::path& metaFilePath, std::string& outGuid, AssetType& outType)
    {
        const json data = JsonManager::GetInstance().LoadJson(Logger::GetInstance().PathToUtf8(metaFilePath));
        if (!data.is_object())
        {
            return false;
        }

        const std::string guid = JsonManager::SafeGet<std::string>(data, "guid", "");
        if (guid.empty())
        {
            return false;
        }

        outGuid = guid;
        outType = StringToAssetType(JsonManager::SafeGet<std::string>(data, "type", ""));
        return true;
    }
}


