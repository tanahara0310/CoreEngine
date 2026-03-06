#pragma once
#include "AssetType.h"
#include <string>
#include <filesystem>
#include <unordered_map>

namespace CoreEngine
{
    // メタファイルの管理クラス
    class AssetMetadata
    {
    public:
        AssetMetadata() = default;
        ~AssetMetadata() = default;

        // メタファイルの読み込み
        // 存在しない場合は新規作成してGUIDを生成
        static std::string LoadOrCreateMetaFile(const std::filesystem::path& assetPath, AssetType type);

        // メタファイルからGUIDを読み取る
        static std::string LoadGUID(const std::filesystem::path& metaFilePath);

        // メタファイルを保存
        static void SaveMetaFile(const std::filesystem::path& assetPath, const std::string& guid, AssetType type);

        // メタファイルの削除
        static void DeleteMetaFile(const std::filesystem::path& assetPath);

        // メタファイルのパスを取得
        static std::filesystem::path GetMetaFilePath(const std::filesystem::path& assetPath);

        // GUIDの生成
        static std::string GenerateGUID();

    private:
        // JSON形式でメタファイルを保存
        static void SaveAsJSON(const std::filesystem::path& metaFilePath, const std::string& guid, AssetType type);

        // JSON形式でメタファイルを読み込み
        static bool LoadFromJSON(const std::filesystem::path& metaFilePath, std::string& outGuid, AssetType& outType);
    };
}
