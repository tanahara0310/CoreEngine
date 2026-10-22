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

        // 画像の取り込み設定（.meta の import）
        struct ImportSettings
        {
            std::string colorSpace; // "srgb" / "linear"。空なら頼まれた色空間で読む
            std::string format;     // "bc7" / "bc3" / "none"。空なら既定の形式
        };

        // メタファイルから画像の取り込み設定を読み取る（書いていない・読めない・ほかの値の項目は空）
        static ImportSettings LoadImportSettings(const std::filesystem::path& metaFilePath);

        // メタファイルの import の色空間と形式を書き換える（GUID などほかの項目は残す。空の項目は消す）
        static bool SaveImportSettings(const std::filesystem::path& metaFilePath, const ImportSettings& settings);

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
