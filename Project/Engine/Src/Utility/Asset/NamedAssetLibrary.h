#pragma once

#include "Utility/Logger/Logger.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file
/// @brief 名前でアセットを引く、キャッシュ付きの読み込み

namespace CoreEngine
{
    /// @brief 名前 → アセットの対応を持つ読み込みキャッシュ
    /// @details 切り替えのたびにディスクを叩かないためのもの。読み込んだアセットは shared_ptr で
    ///          配るので、キャッシュを捨てても使っている側は壊れない。読み込みに失敗した名前も
    ///          nullptr で覚え、毎フレーム引かれても存在しないファイルを探し続けない。
    /// @note メインスレッド専用。ファイルの拡張子は `.json`。
    template <class TAsset>
    class NamedAssetLibrary
    {
    public:
        /// @brief ファイルからアセットを読む（読めたら true）
        using Loader = std::function<bool(const std::string& path, TAsset& out)>;

        /// @brief フォルダの中のファイル名を並べる（拡張子は付いていてよい）
        using Lister = std::function<std::vector<std::string>(const std::string& directory)>;

        /// @param directory 既定で探すフォルダ
        /// @param label ログに出すものの名前（「カメラのリグ」など）
        NamedAssetLibrary(std::string directory, std::string label, Loader loader, Lister lister)
            : directoryPath_(std::move(directory))
            , label_(std::move(label))
            , loader_(std::move(loader))
            , lister_(std::move(lister))
        {
        }

        /// @brief 探すフォルダを設定する（変えるとキャッシュを捨てる）
        void SetDirectory(const std::string& directoryPath)
        {
            if (directoryPath_ == directoryPath) {
                return;
            }
            directoryPath_ = directoryPath;
            ClearCache();
        }

        const std::string& GetDirectory() const { return directoryPath_; }

        /// @brief 名前でアセットを取る
        /// @param name 拡張子なしの名前（`.json` が付いていても受ける）
        /// @return 見つからない・読めないときは nullptr
        std::shared_ptr<const TAsset> Get(const std::string& name)
        {
            if (name.empty()) {
                return nullptr;
            }

            const std::string key = StripExtension(name);
            if (const auto found = cache_.find(key); found != cache_.end()) {
                return found->second;
            }

            const std::string path = ResolvePath(key);
            auto asset = std::make_shared<TAsset>();
            if (!loader_ || !loader_(path, *asset)) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Resource,
                    "{}「{}」を読み込めませんでした: {}", label_, key, path);
                cache_.emplace(key, nullptr);
                return nullptr;
            }

            auto stored = std::shared_ptr<const TAsset>(std::move(asset));
            cache_.emplace(key, stored);
            return stored;
        }

        /// @brief その名前のキャッシュを捨てる（次に取るときに読み直す）
        void Reload(const std::string& name) { cache_.erase(StripExtension(name)); }

        /// @brief キャッシュをすべて捨てる
        void ClearCache() { cache_.clear(); }

        /// @brief フォルダにある名前の一覧（拡張子なし・昇順）
        std::vector<std::string> ListNames() const
        {
            std::vector<std::string> names = lister_ ? lister_(directoryPath_) : std::vector<std::string>{};
            for (std::string& name : names) {
                name = StripExtension(name);
            }
            return names;
        }

    private:
        static constexpr const char* kExtension = ".json";

        /// @brief 末尾の拡張子を取り除いた名前
        static std::string StripExtension(const std::string& name)
        {
            const std::string extension = kExtension;
            if (name.size() > extension.size() &&
                name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
                return name.substr(0, name.size() - extension.size());
            }
            return name;
        }

        /// @brief 名前をファイルのパスにする
        std::string ResolvePath(const std::string& name) const
        {
            return (std::filesystem::path(directoryPath_) / (StripExtension(name) + kExtension)).string();
        }

        std::string directoryPath_;
        std::string label_;
        Loader loader_;
        Lister lister_;
        std::unordered_map<std::string, std::shared_ptr<const TAsset>> cache_;
    };
}
