#include "pch.h"
#include "TextureManager.h"
#include "Cache/TextureCacheStore.h"
#include "Load/TextureLoadPlan.h"
#include "Generate/TextureCubemapGenerator.h"
#include "Load/TextureLoadExecutor.h"
#include "Load/TextureMetadataLoader.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"
#include "Threading/ThreadPool.h"

#include <cassert>
#include <format>
#include <stdexcept>
#include <algorithm>

namespace
{
    constexpr const char* kFallbackTexturePath = "error.png";

    std::string ExtractFileName(const std::string& path)
    {
        const size_t pos = path.find_last_of("/\\");
        if (pos == std::string::npos) {
            return path;
        }
        return path.substr(pos + 1);
    }

    bool IsFallbackTextureRequest(const std::string& requestPath, const std::string& resolvedPath)
    {
        auto& assetDatabase = CoreEngine::AssetDatabase::GetInstance();
        const std::string fallbackAssetPath = assetDatabase.FindAssetPath(kFallbackTexturePath);

        if (!fallbackAssetPath.empty()) {
            if (requestPath == fallbackAssetPath || resolvedPath == fallbackAssetPath) {
                return true;
            }
        }

        return ExtractFileName(requestPath) == kFallbackTexturePath ||
            ExtractFileName(resolvedPath) == kFallbackTexturePath;
    }
}


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

        // キューブマップDDSのフェイスサイズをパス解決器と生成器の両方に適用する。
        // フェイスサイズがパスに組み込まれるため、設定変更時は旧キャッシュが自動的に無視される。
        texturePathResolver_.SetCubemapFaceSize(cubemapFaceSize_);
        cubemapGenerator_.SetOutputFaceSize(cubemapFaceSize_);
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

    std::string TextureManager::MakeCacheKey(const std::string& resolvedPath, TextureColorSpace colorSpace)
    {
        // 同一ファイルでも色空間が異なればGPUリソースの内容が異なるため、キーを分離する。
        return (colorSpace == TextureColorSpace::Linear)
            ? resolvedPath + "|linear"
            : resolvedPath;
    }

    // テクスチャの読み込み
    TextureManager::LoadedTexture TextureManager::Load(const std::string& filePath, TextureColorSpace colorSpace)
    {
        // 入力パスを実体パスに解決し、色空間を加えたキーでキャッシュ検索する。
        std::string resolvedPath = texturePathResolver_.ResolveAssetPath(filePath, false);
        std::string cacheKey = MakeCacheKey(resolvedPath, colorSpace);

        // キャッシュヒット時は即時返却し、重い処理を回避する。
        LoadedTexture cachedTexture{};
        if (cacheStore_->TryGetTexture(cacheKey, cachedTexture)) {
            Logger::GetInstance().Logf(LogLevel::Trace, LogCategory::Resource, "{}", std::format("  Cache hit: {}", cacheKey));
            return cachedTexture;
        }

        // 同一キーの重複ロードを避けるため、ロード権を獲得できるまで待機する。
        while (true) {
            if (cacheStore_->TryGetTexture(cacheKey, cachedTexture)) {
                Logger::GetInstance().Logf(LogLevel::Trace, LogCategory::Resource, "{}", std::format("  Loaded by another thread: {}", cacheKey));
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
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}", std::format("Loading texture: {}", resolvedPath));

        try {
            // 読み込み対象の実パスを事前に計画し、変換やキャッシュ判定を一箇所に集約する。
            TextureLoadPlan::PlanResult loadPlan = textureLoadPlan_.BuildPlan(
                resolvedPath,
                ddsGenerationEnabled,
                texturePathResolver_,
                [this](const std::string& hdrPath, const std::string& cubemapDDSPath) {
                    return cubemapGenerator_.GenerateFromHDR(hdrPath, cubemapDDSPath);
                },
                colorSpace
            );

            resolvedPath = loadPlan.resolvedPath;
            const std::string& ddsPath = loadPlan.ddsPathToGenerate;

            // ロード実行の本体処理は専用クラスに委譲し、Managerはオーケストレーションに集中する。
            TextureLoadExecutor::ExecutionResult executionResult = TextureLoadExecutor::Execute(
                dxCommon,
                resolvedPath,
                ddsGenerationEnabled,
                ddsPath,
                [this, colorSpace](const std::string& sourcePath, const std::string& outputDdsPath) {
                    return ddsCacheGenerator_.GenerateCache(sourcePath, outputDdsPath, colorSpace);
                },
                colorSpace
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
        catch (const std::exception& ex) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Texture,
                "Texture load failed. request='{}' resolved='{}' reason='{}'",
                filePath,
                resolvedPath,
                ex.what());

            if (IsFallbackTextureRequest(filePath, resolvedPath)) {
                Logger::GetInstance().Errorf(
                    LogCategory::Graphics,
                    LogSubCategory::Texture,
                    "Fallback texture '{}' could not be loaded. Engine cannot recover this texture error.",
                    kFallbackTexturePath);
                throw;
            }

            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Texture,
                "Applying fallback texture '{}'. request='{}'",
                kFallbackTexturePath,
                filePath);

            LoadedTexture fallbackTexture = Load(kFallbackTexturePath);
            const std::string fallbackResolvedPath = texturePathResolver_.ResolveAssetPath(kFallbackTexturePath, false);

            DirectX::TexMetadata fallbackMetadata{};
            if (!cacheStore_->TryGetMetadata(fallbackResolvedPath, fallbackMetadata)) {
                fallbackMetadata = TextureMetadataLoader::LoadOrThrow(fallbackResolvedPath);
                cacheStore_->StoreMetadata(fallbackResolvedPath, fallbackMetadata);
            }

            LoadedTexture storedTexture{};
            cacheStore_->StoreIfAbsent(cacheKey, fallbackTexture, fallbackMetadata, storedTexture);

            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Texture,
                "Fallback texture applied. request='{}' fallback='{}'",
                filePath,
                fallbackResolvedPath);

            return storedTexture;
        }
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

        try {
            // キャッシュミス時のI/Oとエラー処理は専用クラスへ委譲する。
            DirectX::TexMetadata texMetadata = TextureMetadataLoader::LoadOrThrow(resolvedPath);

            // 取得済みメタデータをキャッシュ化し、次回のI/Oを削減する。
            cacheStore_->StoreMetadata(cacheKey, texMetadata);

            return texMetadata;
        }
        catch (const std::exception& ex) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Texture,
                "Texture metadata load failed. request='{}' resolved='{}' reason='{}'",
                filePath,
                resolvedPath,
                ex.what());

            if (IsFallbackTextureRequest(filePath, resolvedPath)) {
                throw;
            }

            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Texture,
                "Metadata load fallback to '{}' for request='{}'",
                kFallbackTexturePath,
                filePath);

            Load(kFallbackTexturePath);

            const std::string fallbackResolvedPath = texturePathResolver_.ResolveAssetPath(kFallbackTexturePath, false);
            DirectX::TexMetadata fallbackMetadata{};
            if (!cacheStore_->TryGetMetadata(fallbackResolvedPath, fallbackMetadata)) {
                fallbackMetadata = TextureMetadataLoader::LoadOrThrow(fallbackResolvedPath);
                cacheStore_->StoreMetadata(fallbackResolvedPath, fallbackMetadata);
            }

            cacheStore_->StoreMetadata(cacheKey, fallbackMetadata);
            return fallbackMetadata;
        }
    }

    void TextureManager::Clear()
    {
        // 未完了の非同期ロードを待ってからキャッシュを破棄する。
        WaitForAllPendingLoads();

        if (threadPool_) {
            threadPool_->Shutdown();
            threadPool_.reset();
        }

        cacheStore_->Clear();
    }

    std::unordered_map<std::string, TextureManager::LoadedTexture> TextureManager::GetTextureCache() const
    {
        // デバッグ参照時のデータ競合を避けるため、スナップショットを返す。
        return cacheStore_->GetTextureCache();
    }

    void TextureManager::EnsureThreadPool()
    {
        // 初回の非同期リクエスト時にスレッドプールを生成する。
        if (!threadPool_) {
            uint32_t count = workerThreadCount_;
            if (count == 0) {
                count = (std::max)(1u, std::thread::hardware_concurrency() / 2);
            }
            threadPool_ = std::make_unique<ThreadPool>(count);
            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Resource, "{}",
                std::format("TextureManager: thread pool created with {} workers", count));
        }
    }

    std::shared_future<TextureManager::LoadedTexture> TextureManager::SubmitAsync(const std::string& filePath,
        TextureColorSpace colorSpace)
    {
        // キャッシュヒット時は即座に完了済み future を返す。
        std::string resolvedPath = texturePathResolver_.ResolveAssetPath(filePath, false);
        LoadedTexture cachedTexture{};
        if (cacheStore_->TryGetTexture(MakeCacheKey(resolvedPath, colorSpace), cachedTexture)) {
            std::promise<LoadedTexture> p;
            p.set_value(cachedTexture);
            return p.get_future().share();
        }

        EnsureThreadPool();

        // ワーカースレッドで Load を実行し、結果を future で返す。
        std::string pathCopy = filePath;
        auto future = threadPool_->Submit([this, pathCopy, colorSpace]() -> LoadedTexture {
            return Load(pathCopy, colorSpace);
        });

        auto sharedFuture = future.share();

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pendingFutures_.push_back(sharedFuture);
        }

        return sharedFuture;
    }

    void TextureManager::Load(const std::vector<std::string>& filePaths)
    {
        if (filePaths.empty()) return;

        for (const auto& path : filePaths) {
            SubmitAsync(path);
        }
        WaitForAllPendingLoads();
    }

    void TextureManager::Load(const std::vector<LoadRequest>& requests)
    {
        if (requests.empty()) return;

        for (const auto& request : requests) {
            SubmitAsync(request.path, request.colorSpace);
        }
        WaitForAllPendingLoads();
    }

    void TextureManager::WaitForAllPendingLoads()
    {
        std::vector<std::shared_future<LoadedTexture>> futures;

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            futures = std::move(pendingFutures_);
            pendingFutures_.clear();
        }

        for (auto& f : futures) {
            if (f.valid()) {
                f.wait();
            }
        }
    }

}


