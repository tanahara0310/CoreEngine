#pragma once

#include <externals/DirectXTex/DirectXTex.h>
#include "Graphics/Texture/Runtime/TextureLoadedResource.h"
#include "Graphics/Texture/Path/TexturePathResolver.h"
#include "Graphics/Texture/Load/TextureLoadPlan.h"
#include "Graphics/Texture/Generate/TextureCubemapGenerator.h"
#include "Graphics/Texture/Generate/TextureDdsCacheGenerator.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <memory>
#include <future>
#include <vector>

// 前方宣言
namespace CoreEngine {
    class DirectXCommon;
    class TextureCacheStore;
    class ThreadPool;
}


namespace CoreEngine {

    class TextureManager {
    public:
        using LoadedTexture = TextureLoadedResource;

        // シングルトンアクセス
        static TextureManager& GetInstance();

        // コピー・ムーブを禁止
        TextureManager(const TextureManager&) = delete;
        TextureManager& operator=(const TextureManager&) = delete;
        TextureManager(TextureManager&&) = delete;
        TextureManager& operator=(TextureManager&&) = delete;

        /// @brief 初期化処理
        /// @param dxCommon dxCommonへのポインタ
        void Initialize(CoreEngine::DirectXCommon* dxCommon);

        /// @brief テクスチャの読み込み
        /// @param filePath ファイルパス（Assetsフォルダを省略可能）
        /// @return 読み込まれたテクスチャ
        LoadedTexture Load(const std::string& filePath);

        /// @brief 複数テクスチャの並列読み込み（全完了まで待機）
        /// @param filePaths ファイルパスのリスト
        void Load(const std::vector<std::string>& filePaths);

        /// @brief テクスチャのメタデータを取得
        /// @param filePath ファイルパス（Assetsフォルダを省略可能）
        /// @return テクスチャのメタデータ（幅・高さなど）
        DirectX::TexMetadata GetMetadata(const std::string& filePath);

        /// @brief 初期化済みかどうかを確認
        /// @return 初期化済みならtrue
        bool IsInitialized() const { return isInitialized_; }

        /// @brief 全てのテクスチャをクリア（デバッグ用）
        void Clear();

        /// @brief テクスチャキャッシュのスナップショットを取得
        /// @return テクスチャキャッシュのコピー
        std::unordered_map<std::string, LoadedTexture> GetTextureCache() const;

        /// @brief DDS自動生成を有効化/無効化
        /// @param enable 有効化する場合true
        void SetDDSCacheEnabled(bool enable) { ddsGenerationEnabled_ = enable; }

        /// @brief DDS自動生成が有効かどうか
        /// @return 有効ならtrue
        bool IsDDSCacheEnabled() const { return ddsGenerationEnabled_; }

        /// @brief 非同期ロード用スレッドプールのスレッド数を設定する（Initialize 前に呼ぶ）
        /// @param threadCount スレッド数（0 でハードウェア並列数を自動設定）
        void SetWorkerThreadCount(uint32_t threadCount) { workerThreadCount_ = threadCount; }

    private:
        /// @brief 読み込み時に必要な実行設定をまとめた構造体
        struct LoadContext
        {
            CoreEngine::DirectXCommon* dxCommon = nullptr;
            bool ddsGenerationEnabled = false;
        };

        // プライベートコンストラクタ・デストラクタ
        TextureManager();

        /// @brief 初期化状態を検証し、読み込みに必要な実行設定を取得する
        /// @return 読み込み実行に必要なコンテキスト
        LoadContext AcquireLoadContext() const;

        CoreEngine::DirectXCommon* dxCommon_ = nullptr;
        TexturePathResolver texturePathResolver_;
        TextureLoadPlan textureLoadPlan_;
        TextureCubemapGenerator cubemapGenerator_;
        TextureDdsCacheGenerator ddsCacheGenerator_;
        std::unique_ptr<TextureCacheStore> cacheStore_;
        bool isInitialized_ = false;
        bool ddsGenerationEnabled_ = true; // DDS自動生成を有効化（デフォルト有効）

        // 初期化状態・設定値・デバイスポインタ保護用ミューテックス
        mutable std::mutex cacheMutex_;

        // 非同期ロード用スレッドプール
        std::unique_ptr<ThreadPool> threadPool_;
        uint32_t workerThreadCount_ = 0;

        // 未完了の非同期ロード追跡用
        std::vector<std::shared_future<LoadedTexture>> pendingFutures_;
        std::mutex pendingMutex_;

        /// @brief スレッドプールの遅延初期化
        void EnsureThreadPool();

        /// @brief ワーカースレッドへロードを投入する内部ヘルパー
        std::shared_future<LoadedTexture> SubmitAsync(const std::string& filePath);

        /// @brief 未完了の非同期ロードを全て待機する内部ヘルパー
        void WaitForAllPendingLoads();
    };
}
