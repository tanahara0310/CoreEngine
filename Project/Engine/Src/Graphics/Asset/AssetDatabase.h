#pragma once
#include "AssetInfo.h"
#include "AssetType.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <memory>

namespace CoreEngine
{
    // アセットデータベース（シングルトン）
    class AssetDatabase
    {
    public:
        static AssetDatabase& GetInstance();

        // 初期化：指定ディレクトリをスキャン
        void Initialize(const std::filesystem::path& projectRoot);
        void Finalize();

        // アセットの検索
        // ファイル名のみで検索（最も一般的な使い方）
        std::string FindAssetPath(const std::string& name);

        // カテゴリを指定して検索（同名ファイルの競合解決）
        std::string FindAssetPath(const std::string& name, const std::string& category);

        // GUIDで検索
        std::string FindAssetPathByGUID(const std::string& guid);

        // アセット情報の取得
        const AssetInfo* GetAssetInfo(const std::string& nameOrGuid);
        const AssetInfo* GetAssetInfoByGUID(const std::string& guid);

        // GUIDの取得
        std::string GetGUID(const std::filesystem::path& assetPath);

        // 全アセットの取得（デバッグ用）
        const std::unordered_map<std::string, AssetInfo>& GetAllAssets() const;

        // アセットの再スキャン
        void Refresh();

        // パスの更新（リネーム・移動時）
        void UpdateAssetPath(const std::string& guid, const std::filesystem::path& newPath);

        // アセットの登録
        void RegisterAsset(const std::filesystem::path& assetPath, const std::string& category);

        // アセットの削除
        void UnregisterAsset(const std::string& guid);

        // 統計情報
        size_t GetAssetCount() const { return assetsByGUID_.size(); }
        size_t GetAssetCountByType(AssetType type) const;

        // Library フォルダのパス取得
        std::filesystem::path GetLibraryPath() const;
        std::filesystem::path GetTextureCachePath() const;

        // GUID からキャッシュファイルパスを生成
        std::filesystem::path GetCachedTexturePath(const std::string& guid, const std::string& extension = ".dds") const;

    private:
        AssetDatabase() = default;
        ~AssetDatabase() = default;
        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        // スキャン処理
        void ScanDirectory(const std::filesystem::path& directory, const std::string& category);

        // アセットタイプの判定
        AssetType GetAssetType(const std::filesystem::path& path);

        // ファイルの最終更新時刻を取得
        uint64_t GetFileLastModified(const std::filesystem::path& path);

        std::filesystem::path projectRoot_;

        // GUID -> AssetInfo のマップ
        std::unordered_map<std::string, AssetInfo> assetsByGUID_;

        // ファイル名 -> GUIDs のマップ（同名ファイル対応）
        std::unordered_map<std::string, std::vector<std::string>> assetsByName_;

        // カテゴリ別の優先順位（Application > Engine など）
        std::unordered_map<std::string, int> categoryPriority_;

        bool initialized_ = false;
    };
}
