#pragma once

#include "Graphics/Texture/Runtime/TextureLoadedResource.h"
#include <externals/DirectXTex/DirectXTex.h>

#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <string>

namespace CoreEngine
{
    /// @brief テクスチャ本体キャッシュとメタデータキャッシュの保存責務を分離するクラス
    class TextureCacheStore
    {
    public:
        /// @brief テクスチャキャッシュからキー一致のデータを取得する
        /// @param cacheKey キャッシュ検索キー
        /// @param outTexture 取得結果の出力先
        /// @return ヒット時true
        bool TryGetTexture(const std::string& cacheKey, TextureLoadedResource& outTexture) const;

        /// @brief メタデータキャッシュからキー一致のデータを取得する
        /// @param cacheKey キャッシュ検索キー
        /// @param outMetadata 取得結果の出力先
        /// @return ヒット時true
        bool TryGetMetadata(const std::string& cacheKey, DirectX::TexMetadata& outMetadata) const;

        /// @brief まだ未登録ならテクスチャとメタデータを同時保存する
        /// @param cacheKey キャッシュキー
        /// @param texture 保存対象テクスチャ
        /// @param metadata 保存対象メタデータ
        /// @param outStored 実際に採用されたテクスチャ（既存または新規）
        /// @return 新規保存時true、既存採用時false
        bool StoreIfAbsent(
            const std::string& cacheKey,
            const TextureLoadedResource& texture,
            const DirectX::TexMetadata& metadata,
            TextureLoadedResource& outStored);

        /// @brief 指定キーのロード権を獲得する（既に他スレッドがロード中ならfalse）
        /// @param cacheKey キャッシュキー
        /// @return 獲得できたらtrue
        bool BeginLoad(const std::string& cacheKey);

        /// @brief 指定キーのロード完了を待機する
        /// @param cacheKey キャッシュキー
        void WaitForLoad(const std::string& cacheKey) const;

        /// @brief 指定キーのロード完了を通知する
        /// @param cacheKey キャッシュキー
        void EndLoad(const std::string& cacheKey);

        /// @brief メタデータのみを保存または更新する
        /// @param cacheKey キャッシュキー
        /// @param metadata 保存対象メタデータ
        void StoreMetadata(const std::string& cacheKey, const DirectX::TexMetadata& metadata);

        /// @brief 管理中の全キャッシュを破棄する
        void Clear();

        /// @brief デバッグ参照用にテクスチャキャッシュを返す
        /// @return テクスチャキャッシュ参照
        std::unordered_map<std::string, TextureLoadedResource> GetTextureCache() const;

    private:
        std::unordered_map<std::string, TextureLoadedResource> textureCache_;
        std::unordered_map<std::string, DirectX::TexMetadata> metadataCache_;
        mutable std::mutex cacheMutex_;
        mutable std::condition_variable loadCondition_;
        std::unordered_set<std::string> loadingKeys_;
    };
}
