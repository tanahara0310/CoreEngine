#include "TextureCacheStore.h"

#include <chrono>

namespace CoreEngine
{
    bool TextureCacheStore::TryGetTexture(const std::string& cacheKey, TextureLoadedResource& outTexture) const
    {
        // キャッシュ読み出しはロック下で行い、コピー後にロックを解放する。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = textureCache_.find(cacheKey);
        if (it == textureCache_.end()) {
            return false;
        }

        outTexture = it->second;
        return true;
    }

    bool TextureCacheStore::TryGetMetadata(const std::string& cacheKey, DirectX::TexMetadata& outMetadata) const
    {
        // メタデータも同一ストアで保護し、整合性のある値を返す。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = metadataCache_.find(cacheKey);
        if (it == metadataCache_.end()) {
            return false;
        }

        outMetadata = it->second;
        return true;
    }

    bool TextureCacheStore::StoreIfAbsent(
        const std::string& cacheKey,
        const TextureLoadedResource& texture,
        const DirectX::TexMetadata& metadata,
        TextureLoadedResource& outStored)
    {
        // 二重登録競合を防ぐため、存在確認と保存を同じロック区間で実行する。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = textureCache_.find(cacheKey);
        if (it != textureCache_.end()) {
            outStored = it->second;
            return false;
        }

        textureCache_[cacheKey] = texture;
        metadataCache_[cacheKey] = metadata;
        outStored = texture;
        return true;
    }

    bool TextureCacheStore::BeginLoad(const std::string& cacheKey)
    {
        // 既に他スレッドが同一キーをロード中ならfalseを返し、重複ロードを防ぐ。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (loadingKeys_.contains(cacheKey)) {
            return false;
        }

        loadingKeys_.insert(cacheKey);
        return true;
    }

    void TextureCacheStore::WaitForLoad(const std::string& cacheKey)
    {
        // ロード中キーが解放されるまで待機し、完了後に再度キャッシュ確認できるようにする。
        // 例外終了等でEndLoadが呼ばれないケースに備え、無限待機を避ける。
        std::unique_lock<std::mutex> lock(cacheMutex_);

        constexpr auto kWaitSlice = std::chrono::milliseconds(100);
        constexpr auto kMaxWait = std::chrono::seconds(5);
        auto waited = std::chrono::milliseconds::zero();

        while (loadingKeys_.contains(cacheKey)) {
            if (loadCondition_.wait_for(lock, kWaitSlice, [this, &cacheKey]() {
                return !loadingKeys_.contains(cacheKey);
            })) {
                break;
            }

            waited += std::chrono::duration_cast<std::chrono::milliseconds>(kWaitSlice);
            if (waited >= kMaxWait) {
                loadingKeys_.erase(cacheKey);
                loadCondition_.notify_all();
                break;
            }
        }
    }

    void TextureCacheStore::EndLoad(const std::string& cacheKey)
    {
        // ロード終了を待機中スレッドへ通知し、次の取得/ロードへ進めるようにする。
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            loadingKeys_.erase(cacheKey);
        }
        loadCondition_.notify_all();
    }

    void TextureCacheStore::StoreMetadata(const std::string& cacheKey, const DirectX::TexMetadata& metadata)
    {
        // メタデータのみ先行保存したい呼び出しに対応する。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        metadataCache_[cacheKey] = metadata;
    }

    void TextureCacheStore::Clear()
    {
        // テクスチャとメタデータを同時に消して、キャッシュ整合性を維持する。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        textureCache_.clear();
        metadataCache_.clear();
        loadingKeys_.clear();
        loadCondition_.notify_all();
    }

    std::unordered_map<std::string, TextureLoadedResource> TextureCacheStore::GetTextureCache() const
    {
        // 呼び出し側で安全に扱えるよう、ロック下でスナップショットを返す。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        return textureCache_;
    }
}
