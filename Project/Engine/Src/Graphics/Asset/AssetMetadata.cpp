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


