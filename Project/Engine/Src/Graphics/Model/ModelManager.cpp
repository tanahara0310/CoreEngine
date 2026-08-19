#include "pch.h"
#include "ModelManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Primitive/IPrimitiveMeshGenerator.h"
#include "Graphics/Render/Model/Instancing/InstanceBatchManager.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Model/Skeleton/SkinningComputeDispatcher.h"
#include "Graphics/Pipeline/CustomShaderPipelineCache.h"
#include "Graphics/Common/EngineStats.h"
#include "Animation/AnimationLoader.h"
#include "Animation/AnimationPlayer.h"
#include "Animation/SkeletonAnimatorFactory.h"
#include "Threading/ThreadPool.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <filesystem>
#include <algorithm>
#include <thread>

namespace CoreEngine
{
    ModelManager::ModelManager() = default;
    ModelManager::~ModelManager()
    {
        // 走行中のワーカーが dxCommon_ / TextureManager を触っている可能性があるので、
        // メンバ（threadPool_ 含む）を壊す前に必ず合流させる
        WaitForPreload();
    }

    void ModelManager::Initialize(DirectXCommon* dxCommon, ResourceFactory* factory)
    {
        assert(dxCommon && factory);
        dxCommon_ = dxCommon;
        resourceFactory_ = factory;
        customShaderPipelineCache_ = std::make_unique<CustomShaderPipelineCache>();
    }

    void ModelManager::SetRenderContext(const ModelRenderContext& ctx)
    {
        assert(ctx.IsValid() && "ModelRenderContext must be fully initialized before setting on ModelManager");
        renderContext_ = ctx;

        // InstanceBatchManager を生成（フレーム数は3、最大インスタンス数は10000と仮定）
        constexpr uint32_t kFrameCount = 3;
        constexpr uint32_t kMaxInstancesPerFrame = 10000;
        instanceBatchManager_ = std::make_unique<InstanceBatchManager>();
        instanceBatchManager_->Initialize(dxCommon_, kFrameCount, kMaxInstancesPerFrame);

        // コンテキストにバッチマネージャーを追加
        renderContext_.instanceBatchManager = instanceBatchManager_.get();

        // レンダラーにもバッチマネージャーを設定
        if (renderContext_.modelRenderer) {
            renderContext_.modelRenderer->SetInstanceBatchManager(instanceBatchManager_.get());
        }

        // GPUスキニング(CS)ディスパッチャーを生成
        skinningDispatcher_ = std::make_unique<SkinningComputeDispatcher>();
        skinningDispatcher_->Initialize(dxCommon_->GetDevice());
        renderContext_.skinningDispatcher = skinningDispatcher_.get();
    }

    std::unique_ptr<Model> ModelManager::CreateStaticModel(const std::string& filePath)
    {
        assert(IsInitialized());

        // パスを解決
        std::string resolvedPath = ResolveFilePath(filePath);

        std::string directoryPath, filename;
        SplitPath(resolvedPath, directoryPath, filename);

        // リソースを取得または読み込み
        ModelResource* resource = LoadModelResourceInternal(directoryPath, filename);
        assert(resource && resource->IsLoaded());

        // アニメーションコントローラーなしでインスタンスを作成
        auto instance = std::make_unique<Model>();
        instance->Initialize(resource, renderContext_);

        return instance;
    }

    std::unique_ptr<Model> ModelManager::CreateSkeletonModel(
        const std::string& filePath,
        const std::string& animationName,
        bool loop
    )
    {
        assert(IsInitialized());

        // パスを解決
        std::string resolvedPath = ResolveFilePath(filePath);

        std::string directoryPath, filename;
        SplitPath(resolvedPath, directoryPath, filename);

        // リソースを取得または読み込み
        ModelResource* resource = LoadModelResourceInternal(directoryPath, filename);
        assert(resource && resource->IsLoaded());

        auto instance = std::make_unique<Model>();
        instance->Initialize(resource, renderContext_);

        // スケルトンがない場合は静的モデルとして返す
        if (!resource->GetSkeleton()) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}",
                "CreateSkeletonModel: model has no skeleton, created as static model: " + filePath);
            return instance;
        }

        // アニメーションを取得（名前が空の場合は最初のアニメーション）
        const Animation* animation = resource->GetAnimation(animationName);
        if (!animation) {
            // アニメーションが見つからない場合は静的モデルとして返す
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}",
                "CreateSkeletonModel: animation not found, created as static model: " + filePath);
            return instance;
        }

        // 初期アニメーションのコントローラーと切り替え用ファクトリーを持つプレイヤーを注入する
        auto factory = std::make_unique<SkeletonAnimatorFactory>();
        auto skeletonAnimator = factory->CreateSkeletonAnimator(*resource->GetSkeleton(), *animation, loop);
        instance->SetAnimationPlayer(std::make_unique<AnimationPlayer>(
            resource, std::move(skeletonAnimator), std::move(factory)));

        return instance;
    }

    bool ModelManager::LoadAnimation(const AnimationLoadInfo& loadInfo)
    {
        std::string resolvedModelPath = ResolveFilePath(loadInfo.modelFile);

        std::string resolvedDirectory, resolvedFilename;
        SplitPath(resolvedModelPath, resolvedDirectory, resolvedFilename);

        // モデルリソースを取得（キャッシュになければ読み込む）
        ModelResource* resource = LoadModelResourceInternal(resolvedDirectory, resolvedFilename);
        if (!resource) {
            return false;
        }

        const std::string& animFile = loadInfo.animationFile.empty()
            ? resolvedFilename
            : loadInfo.animationFile;

        Animation animation = AnimationLoader::LoadAnimationFile(
            resolvedDirectory, animFile, loadInfo.sourceAnimationName);
        resource->AddAnimation(loadInfo.animationName, animation);
        return true;
    }

    void ModelManager::ClearCache()
    {
        // 先読み中のリソースをキャッシュから消すと、完了したワーカーが
        // 消えた後のエントリへ書き戻して迷子になる。先に合流させる
        WaitForPreload();

        std::lock_guard<std::mutex> lock(cacheMutex_);
        resourceCache_.clear();
    }

    void ModelManager::ForEachResource(const std::function<void(ModelResource*)>& callback)
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (auto& [key, resource] : resourceCache_) {
            if (resource && resource->IsLoaded()) {
                callback(resource.get());
            }
        }
    }

    std::unique_ptr<Model> ModelManager::CreatePrimitiveModel(const std::string& key, const IPrimitiveMeshGenerator& generator)
    {
        assert(IsInitialized());

        // キャッシュ確認
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = resourceCache_.find(key);
            if (it != resourceCache_.end()) {
                auto instance = std::make_unique<Model>();
                instance->Initialize(it->second.get(), renderContext_);
                return instance;
            }
        }

        // メッシュデータを生成してリソースを作成
        ModelData modelData = generator.Generate();

        auto resource = std::make_unique<ModelResource>();
        auto& textureManager = TextureManager::GetInstance();
        resource->Initialize(dxCommon_, resourceFactory_, &textureManager);
        resource->LoadFromModelData(std::move(modelData), key);

        ModelResource* resourcePtr = nullptr;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto [it, inserted] = resourceCache_.emplace(key, std::move(resource));
            resourcePtr = it->second.get();
        }

        auto instance = std::make_unique<Model>();
        instance->Initialize(resourcePtr, renderContext_);
        return instance;
    }

    ModelResource* ModelManager::LoadModelResourceInternal(const std::string& directoryPath, const std::string& filename)
    {
        assert(IsInitialized());

        const std::string normalizedPath = MakeNormalizedPath(directoryPath, filename);

        // Phase 1: キャッシュ確認 + ロード権の確保
        while (true) {
            std::unique_lock<std::mutex> lock(cacheMutex_);

            // キャッシュヒット
            auto it = resourceCache_.find(normalizedPath);
            if (it != resourceCache_.end()) {
                EngineStats::GetInstance().RecordCacheHit();
                return it->second.get();
            }

            // 別スレッドが同じリソースをロード中なら完了まで待機
            if (loadingPaths_.count(normalizedPath) > 0) {
                cacheCondVar_.wait(lock, [&]() {
                    return resourceCache_.count(normalizedPath) > 0 ||
                        loadingPaths_.count(normalizedPath) == 0;
                    });
                continue; // 再チェック
            }

            // ロード権を確保して抜ける（キャッシュミス確定）
            EngineStats::GetInstance().RecordCacheMiss();
            loadingPaths_.insert(normalizedPath);
            break;
        }

        // Phase 2: ミューテックスを持たずにロード（重い処理）
        std::unique_ptr<ModelResource> resource;
        try {
            resource = std::make_unique<ModelResource>();
            auto& textureManager = TextureManager::GetInstance();
            resource->Initialize(dxCommon_, resourceFactory_, &textureManager);
            resource->LoadFromFile(directoryPath, filename);
        }
        catch (...) {
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                loadingPaths_.erase(normalizedPath);
            }
            cacheCondVar_.notify_all();
            throw;
        }

        // Phase 3: キャッシュ登録 + ロード権の解放
        ModelResource* resourcePtr = nullptr;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            loadingPaths_.erase(normalizedPath);
            auto [it, inserted] = resourceCache_.emplace(normalizedPath, std::move(resource));
            resourcePtr = it->second.get();
        }
        cacheCondVar_.notify_all();

        return resourcePtr;
    }

    void ModelManager::PreloadModels(const std::vector<std::string>& filePaths)
    {
        BeginPreload(filePaths);
        WaitForPreload();
    }

    void ModelManager::BeginPreload(const std::vector<std::string>& filePaths)
    {
        if (filePaths.empty()) return;

        EnsureThreadPool();

        std::lock_guard<std::mutex> lock(preloadMutex_);
        preloadFutures_.reserve(preloadFutures_.size() + filePaths.size());

        for (const auto& path : filePaths) {
            preloadFutures_.push_back(threadPool_->Submit(
                "Model: " + std::filesystem::path(path).filename().string(),
                [this, path]() {
                // 例外はここで止める。future に載せて後で get() の場所まで運ぶと、
                // 起動シーケンスと無関係な地点で飛んで原因が分からなくなる。
                // 先読みはあくまで最適化なので、失敗しても本番のロードに任せればよい。
                try {
                    std::string resolved = ResolveFilePath(path);
                    std::string dir, file;
                    SplitPath(resolved, dir, file);
                    LoadModelResourceInternal(dir, file);
                }
                catch (const std::exception& e) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "モデル先読みに失敗（本番ロードで再試行されます）: {} ({})", path, e.what());
                }
                catch (...) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Resource,
                        "モデル先読みに失敗（本番ロードで再試行されます）: {}", path);
                }
                }));
        }
    }

    void ModelManager::WaitForPreload()
    {
        // ワーカーが完了報告のために preloadMutex_ を取ることは無いが、
        // 待っている間に BeginPreload が追加できるよう、ロックは取り出しの間だけにする
        std::vector<std::future<void>> futures;
        {
            std::lock_guard<std::mutex> lock(preloadMutex_);
            futures.swap(preloadFutures_);
        }

        for (auto& f : futures) {
            if (f.valid()) {
                // Wait は待つ代わりにキューのタスクを引き受ける。
                // モデルロードは内部でテクスチャロードを待つので、この待ちが
                // ワーカー上で起きるとプールが自分自身を待って詰まりうる
                if (threadPool_) {
                    threadPool_->Wait(f);
                }
                f.get();
            }
        }
    }

    void ModelManager::EnsureThreadPool()
    {
        if (!threadPool_) {
            // ワーカー数は ThreadBudget が決める（プール乱立の抑制）
            ThreadPoolDesc poolDesc;
            poolDesc.name = "ModelLoad";
            poolDesc.priority = WorkerPriority::Normal;
            threadPool_ = std::make_unique<ThreadPool>(poolDesc);
        }
    }

    std::string ModelManager::MakeNormalizedPath(const std::string& directoryPath, const std::string& filename) const
    {
        // ディレクトリパスとファイル名を結合
        std::string fullPath = directoryPath;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\') {
            fullPath += "/";
        }
        fullPath += filename;

        // UTF-8 の文字列と path の往復は必ず Logger の Utf8ToPath / PathToUtf8 を通す。
        // path(std::string) と path::string() は ANSI 変換なので、非 ASCII のファイル名が壊れる。
        // PathToUtf8 は区切りも '/' に正規化する。
        Logger& log = Logger::GetInstance();
        return log.PathToUtf8(log.Utf8ToPath(fullPath).lexically_normal());
    }

    void ModelManager::SplitPath(const std::string& filePath, std::string& outDirectory, std::string& outFilename) const
    {
        // 入出力とも UTF-8。往復は MakeNormalizedPath と同じ理由で Logger を通す。
        Logger& log = Logger::GetInstance();
        const std::filesystem::path path = log.Utf8ToPath(filePath);
        outDirectory = log.PathToUtf8(path.parent_path());
        outFilename = log.PathToUtf8(path.filename());
    }

    ModelResource* ModelManager::GetModelResource(const std::string& filePath)
    {
        std::string resolvedPath = ResolveFilePath(filePath);

        std::string directoryPath, filename;
        SplitPath(resolvedPath, directoryPath, filename);

        std::string normalizedPath = MakeNormalizedPath(directoryPath, filename);

        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = resourceCache_.find(normalizedPath);
        if (it != resourceCache_.end()) {
            return it->second.get();
        }

        return nullptr;
    }

    std::string ModelManager::ResolveFilePath(const std::string& filePath) const
    {
        // 入力パスのバックスラッシュをスラッシュに統一
        std::string normalized = filePath;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');

        // まずAssetDatabaseで名前解決（移動・リネーム耐性）
        // この関数が扱う std::string は一貫して UTF-8。path との往復は Logger の
        // Utf8ToPath / PathToUtf8 を通し、ANSI コードページを混入させない。
        Logger& log = Logger::GetInstance();
        std::filesystem::path inputPath = log.Utf8ToPath(normalized);
        std::string searchName = log.PathToUtf8(inputPath.filename());
        if (searchName.empty()) {
            searchName = normalized;
        }

        auto& assetDB = AssetDatabase::GetInstance();
        std::filesystem::path assetPath = assetDB.FindAssetPath(searchName);
        if (assetPath.empty() && inputPath.has_stem()) {
            assetPath = assetDB.FindAssetPath(log.PathToUtf8(inputPath.stem()));
        }
        if (!assetPath.empty()) {
            return log.PathToUtf8(assetPath);
        }

        // Application/Assets または Engine/Assets で始まる場合はそのまま返す
        if (normalized.starts_with("Application/Assets/")) {
            return normalized;
        }
        if (normalized.starts_with("Engine/Assets/")) {
            return normalized;
        }

        // 絶対パス（C:/ など）の場合はそのまま返す
        if (normalized.length() >= 2 && normalized[1] == ':') {
            return normalized;
        }

        // それ以外の場合はbasePath_を前に追加
        return basePath_ + normalized;
    }

    void ModelManager::UpdateResourceCacheStats()
    {
        auto& cacheStats = EngineStats::GetInstance().GetResourceCacheStats();
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cacheStats.loadedModelCount = static_cast<uint32_t>(resourceCache_.size());
        cacheStats.loadingResourceCount = static_cast<uint32_t>(loadingPaths_.size());
    }
}
