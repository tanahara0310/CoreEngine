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
    class ThreadPool;

    // アセットデータベース
    class AssetDatabase
    {
    public:
        static AssetDatabase& GetInstance();

        /// @brief 初期化：指定ディレクトリをスキャン
        void Initialize(const std::filesystem::path& projectRoot);

        /// @brief 終了処理
        void Finalize();

        /// @brief ファイル名でアセットパスを検索
        /// @param name 検索キー（ファイル名・ステム。パスではなく照合用の名前）
        /// @return 見つかった絶対パス。見つからなければ空の path
        /// @details 戻り値を narrow 文字列に落とさないこと。path のまま運べば
        ///          エンコーディングを決めるのは OS API へ渡す直前の1箇所だけになり、
        ///          ANSI と UTF-8 の取り違えが起こらない。
        std::filesystem::path FindAssetPath(const std::string& name);

        /// @brief ファイルパスから GUID を取得
        std::string GetGUID(const std::filesystem::path& assetPath);

        /// @brief アセットの再スキャン
        void Refresh();

        /// @brief テクスチャキャッシュフォルダのパス取得
        std::filesystem::path GetTextureCachePath() const;

        /// @brief GUID からキャッシュファイルパスを生成
        std::filesystem::path GetCachedTexturePath(const std::string& guid, const std::string& extension = ".dds") const;

        /// @brief シェーダーファイルが存在するディレクトリ一覧を重複なしで返す
        std::vector<std::filesystem::path> GetShaderIncludeDirectories() const;

    private:
        AssetDatabase() = default;
        ~AssetDatabase() = default;
        AssetDatabase(const AssetDatabase&) = delete;
        AssetDatabase& operator=(const AssetDatabase&) = delete;

        /// @brief ファイル1件分の AssetInfo を構築する（スレッドセーフ）
        std::optional<AssetInfo> BuildAssetInfo(
            const std::filesystem::path& assetPath,
            const std::string& category) const;

        /// @brief 構築済みの AssetInfo を内部インデックスに登録する
        void MergeAssetInfo(AssetInfo&& info);

        static AssetType GetAssetType(const std::filesystem::path& path);
        static uint64_t GetFileLastModified(const std::filesystem::path& path);
        std::filesystem::path GetLibraryPath() const;

        std::filesystem::path projectRoot_;

        // GUID -> AssetInfo のマップ
        std::unordered_map<std::string, AssetInfo> assetsByGUID_;

        // ファイル名 -> GUIDs のマップ（同名ファイル対応）
        std::unordered_map<std::string, std::vector<std::string>> assetsByName_;

        // カテゴリ別の優先順位（Application > Engine など）
        std::unordered_map<std::string, int> categoryPriority_;

        bool initialized_ = false;

        // 並列スキャン用スレッドプール（初回スキャン時に生成、完了後解放）
        std::unique_ptr<ThreadPool> threadPool_;
    };
}
