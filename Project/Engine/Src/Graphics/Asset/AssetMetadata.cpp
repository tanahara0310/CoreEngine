#include "AssetMetadata.h"
#include "Utility/Logger/Logger.h"
#include <fstream>
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
        }

        // 存在しない場合は新規作成
        std::string newGuid = GenerateGUID();
        SaveMetaFile(assetPath, newGuid, type);

        Logger::GetInstance().Log(
            "Created meta file for: " + assetPath.filename().string() + " (GUID: " + newGuid + ")",
            LogLevel::INFO, LogCategory::System
        );

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
        try
        {
            std::ofstream file(metaFilePath);
            if (!file.is_open())
            {
                Logger::GetInstance().Log(
                    "Failed to create meta file: " + metaFilePath.string(),
                    LogLevel::Error, LogCategory::System
                );
                return;
            }

            // 簡易的なJSON形式で保存
            file << "{\n";
            file << "  \"guid\": \"" << guid << "\",\n";
            file << "  \"type\": \"" << AssetTypeToString(type) << "\"\n";
            file << "}\n";

            file.close();

            // Windows: メタファイルを隠しファイルに設定
#ifdef _WIN32
            std::wstring metaFilePathW(metaFilePath.wstring());
            DWORD attributes = GetFileAttributesW(metaFilePathW.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES)
            {
                // 隠しファイル属性を追加
                SetFileAttributesW(metaFilePathW.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
            }
#endif
        }
        catch (const std::exception& e)
        {
            Logger::GetInstance().Log(
                "Exception while saving meta file: " + std::string(e.what()),
                LogLevel::Error, LogCategory::System
            );
        }
    }

    bool AssetMetadata::LoadFromJSON(const std::filesystem::path& metaFilePath, std::string& outGuid, AssetType& outType)
    {
        try
        {
            std::ifstream file(metaFilePath);
            if (!file.is_open())
            {
                return false;
            }

            std::string line;
            std::string guid;
            std::string typeStr;

            // 簡易的なJSON解析
            while (std::getline(file, line))
            {
                // "guid" フィールドを探す
                size_t guidPos = line.find("\"guid\"");
                if (guidPos != std::string::npos)
                {
                    size_t startQuote = line.find("\"", guidPos + 7);
                    size_t endQuote = line.find("\"", startQuote + 1);
                    if (startQuote != std::string::npos && endQuote != std::string::npos)
                    {
                        guid = line.substr(startQuote + 1, endQuote - startQuote - 1);
                    }
                }

                // "type" フィールドを探す
                size_t typePos = line.find("\"type\"");
                if (typePos != std::string::npos)
                {
                    size_t startQuote = line.find("\"", typePos + 7);
                    size_t endQuote = line.find("\"", startQuote + 1);
                    if (startQuote != std::string::npos && endQuote != std::string::npos)
                    {
                        typeStr = line.substr(startQuote + 1, endQuote - startQuote - 1);
                    }
                }
            }

            file.close();

            if (!guid.empty())
            {
                outGuid = guid;
                outType = StringToAssetType(typeStr);
                return true;
            }

            return false;
        }
        catch (const std::exception& e)
        {
            Logger::GetInstance().Log(
                "Exception while loading meta file: " + std::string(e.what()),
                LogLevel::Error, LogCategory::System
            );
            return false;
        }
    }
}
