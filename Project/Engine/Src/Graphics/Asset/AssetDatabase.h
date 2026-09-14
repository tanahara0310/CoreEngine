#pragma once
#include "AssetInfo.h"
#include "AssetType.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <memory>
#include <string_view>

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
        /// @note 戻り値を narrow 文字列に落とさないこと（ANSI と UTF-8 の取り違えを避けるため）
        std::filesystem::path FindAssetPath(const std::string& name);

        /// @brief ファイルパスから GUID を取得
        std::string GetGUID(const std::filesystem::path& assetPath);

        /// @brief GUID でアセット情報を引く
        /// @return 見つからなければ nullptr
        const AssetInfo* FindAssetByGUID(const std::string& guid) const;

        /// @brief パスでアセット情報を引く
        /// @param path プロジェクトの根からの相対パス（`Application/Assets/` を省いたものも可）か絶対パス。UTF-8
        /// @return 見つからなければ nullptr
        const AssetInfo* FindAssetByPath(std::string_view path) const;

        /// @brief 指定した種類のアセット情報を相対パス順に返す
        std::vector<const AssetInfo*> GetAssetsOfType(AssetType type) const;

        /// @brief ファイルを 1 件登録する（登録済みならその情報を返す）
        /// @param assetPath プロジェクトの根からの相対パスか絶対パス
        /// @return 登録できない種類・存在しないファイルなら nullptr
        const AssetInfo* ImportAsset(const std::filesystem::path& assetPath);

        /// @brief 登録内容が変わるたびに進む番号
        uint64_t GetRevision() const noexcept { return revision_; }

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

        // 相対パス（区切りは '/'、ASCII の英字は小文字）-> GUID のマップ
        std::unordered_map<std::string, std::string> guidsByPath_;

        // カテゴリ別の優先順位（Application > Engine など）
        std::unordered_map<std::string, int> categoryPriority_;

        bool initialized_ = false;

        // 登録内容が変わるたびに進む番号
        uint64_t revision_ = 0;

        // 並列スキャン用スレッドプール（初回スキャン時に生成、完了後解放）
        std::unique_ptr<ThreadPool> threadPool_;
    };
}
