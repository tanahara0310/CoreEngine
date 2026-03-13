#include "TextureManager.h"
#include "Cache/TextureCacheStore.h"
#include "Load/TextureLoadPlan.h"
#include "Generate/TextureCubemapGenerator.h"
#include "Load/TextureLoadExecutor.h"
#include "Load/TextureMetadataLoader.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <format>
#include <stdexcept>


namespace CoreEngine
{
    TextureManager::TextureManager()
        : cacheStore_(std::make_unique<TextureCacheStore>()) {
        // キャッシュストアを明示的に初期化し、責務分離後の依存関係を確定させる。
    }


    TextureManager& TextureManager::GetInstance()
    {
        static TextureManager instance;
        return instance;
    }

    // 初期化
    void TextureManager::Initialize(CoreEngine::DirectXCommon* dxCommon)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);

        if (dxCommon == nullptr) {
            throw std::invalid_argument("TextureManager::Initialize received null DirectXCommon");
        }

        dxCommon_ = dxCommon;
        isInitialized_ = true;
    }

    TextureManager::LoadContext TextureManager::AcquireLoadContext() const
    {
        // 初期化済みであることを保証した上で、実行時設定を一括で取得する。
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (!isInitialized_ || dxCommon_ == nullptr) {
            throw std::logic_error("TextureManager is not initialized");
        }

        LoadContext context{};
        context.dxCommon = dxCommon_;
        context.ddsGenerationEnabled = ddsGenerationEnabled_;
        return context;
    }

    // テクスチャの読み込み
    TextureManager::LoadedTexture TextureManager::Load(const std::string& filePath)
    {
        // 入力パスを実体パスに解決し、キャッシュ検索キーとして扱う。
        std::string resolvedPath = texturePathResolver_.ResolveAssetPath(filePath, true);
        std::string cacheKey = resolvedPath;

        // キャッシュヒット時は即時返却し、重い処理を回避する。
        LoadedTexture cachedTexture{};
        if (cacheStore_->TryGetTexture(cacheKey, cachedTexture)) {
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", std::format("Texture already loaded (cache hit): {}", cacheKey));
            return cachedTexture;
        }

        // 同一キーの重複ロードを避けるため、ロード権を獲得できるまで待機する。
        while (true) {
            if (cacheStore_->TryGetTexture(cacheKey, cachedTexture)) {
                Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", std::format("Texture loaded by another thread: {}", cacheKey));
                return cachedTexture;
            }

            if (cacheStore_->BeginLoad(cacheKey)) {
                break;
            }

            cacheStore_->WaitForLoad(cacheKey);
        }

        struct ScopedLoadNotify {
            TextureCacheStore* store;
            std::string key;
            ~ScopedLoadNotify() {
                store->EndLoad(key);
            }
        } notify{ cacheStore_.get(), cacheKey };

        // 初期化状態と実行設定の取得を共通化して処理の重複を避ける。
        LoadContext loadContext = AcquireLoadContext();
        CoreEngine::DirectXCommon* dxCommon = loadContext.dxCommon;
        bool ddsGenerationEnabled = loadContext.ddsGenerationEnabled;

        assert(dxCommon != nullptr);

        // ロード開始ログ
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", std::format("Loading texture: {}", resolvedPath));

        // 読み込み対象の実パスを事前に計画し、変換やキャッシュ判定を一箇所に集約する。
        TextureLoadPlan::PlanResult loadPlan = textureLoadPlan_.BuildPlan(
            resolvedPath,
            ddsGenerationEnabled,
            texturePathResolver_,
            [this](const std::string& hdrPath, const std::string& cubemapDDSPath) {
                return cubemapGenerator_.GenerateFromHDR(hdrPath, cubemapDDSPath);
            }
        );

        resolvedPath = loadPlan.resolvedPath;
        bool isDDS = loadPlan.isDDS;
        bool isHDR = loadPlan.isHDR;
        const std::string& ddsPath = loadPlan.ddsPathToGenerate;

        // ロード実行の本体処理は専用クラスに委譲し、Managerはオーケストレーションに集中する。
        TextureLoadExecutor::ExecutionResult executionResult = TextureLoadExecutor::Execute(
            dxCommon,
            resolvedPath,
            isDDS,
            isHDR,
            ddsGenerationEnabled,
            ddsPath,
            [this](const std::string& sourcePath, const std::string& outputDdsPath) {
                return ddsCacheGenerator_.GenerateCache(sourcePath, outputDdsPath);
            }
        );

        LoadedTexture result{};

        // 結果をLoadedTexture形式へ詰め替えて、既存インターフェースを維持する。
        result.texture = executionResult.uploadResult.texture;
        result.intermediate = executionResult.uploadResult.intermediate;
        result.cpuHandle = executionResult.uploadResult.cpuHandle;
        result.gpuHandle = executionResult.uploadResult.gpuHandle;

        // 最終登録時もストア側で重複登録競合を吸収する。
        LoadedTexture storedTexture{};
        cacheStore_->StoreIfAbsent(cacheKey, result, executionResult.metadata, storedTexture);
        return storedTexture;
    }

    DirectX::TexMetadata TextureManager::GetMetadata(const std::string& filePath)
    {
        // 初期化チェックを共通処理に寄せ、取得処理は下流へ集中させる。
        AcquireLoadContext();

        std::string resolvedPath = texturePathResolver_.ResolveAssetPath(filePath);
        std::string cacheKey = resolvedPath;

        // メタデータ専用キャッシュを参照し、ヒット時は即返却する。
        DirectX::TexMetadata cachedMetadata{};
        if (cacheStore_->TryGetMetadata(cacheKey, cachedMetadata)) {
            return cachedMetadata;
        }

        // キャッシュミス時のI/Oとエラー処理は専用クラスへ委譲する。
        DirectX::TexMetadata texMetadata = TextureMetadataLoader::LoadOrThrow(resolvedPath);

        // 取得済みメタデータをキャッシュ化し、次回のI/Oを削減する。
        cacheStore_->StoreMetadata(cacheKey, texMetadata);

        return texMetadata;
    }

    void TextureManager::Clear()
    {
        // キャッシュの破棄は専用ストアへ委譲し、管理責務を集約する。
        cacheStore_->Clear();
    }

    std::unordered_map<std::string, TextureManager::LoadedTexture> TextureManager::GetTextureCache() const
    {
        // デバッグ参照時のデータ競合を避けるため、スナップショットを返す。
        return cacheStore_->GetTextureCache();
    }

}


