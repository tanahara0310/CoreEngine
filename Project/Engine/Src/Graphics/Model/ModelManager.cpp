#include "ModelManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Animation/AnimationLoader.h"
#include "Animation/Animator.h"
#include "Animation/SkeletonAnimatorFactory.h"
#include "Threading/ThreadPool.h"

#include <cassert>
#include <filesystem>
#include <algorithm>
#include <thread>


namespace CoreEngine
{
void ModelManager::Initialize(DirectXCommon* dxCommon, ResourceFactory* factory)
{
    assert(dxCommon && factory);
    dxCommon_ = dxCommon;
    resourceFactory_ = factory;
}

void ModelManager::SetRenderContext(const ModelRenderContext& ctx)
{
    assert(ctx.IsValid() && "ModelRenderContext must be fully initialized before setting on ModelManager");
    renderContext_ = ctx;
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

std::unique_ptr<Model> ModelManager::CreateKeyframeModel(
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

    // アニメーションを取得
    std::string animName = animationName;
    if (animName.empty() && resource->HasAnimation()) {
        const auto& animations = resource->GetAnimations();
        if (!animations.empty()) {
            animName = animations.begin()->first;
        }
    }

    const Animation* animation = resource->GetAnimation(animName);
    if (!animation) {
        // アニメーションが見つからない場合は静的モデルとして作成
        auto instance = std::make_unique<Model>();
        instance->Initialize(resource, renderContext_);
        return instance;
    }

    // Animatorを作成
    auto animator = std::make_unique<Animator>();
    animator->SetAnimation(*animation);
    animator->SetLooping(loop);

    // インスタンスを作成
    auto instance = std::make_unique<Model>();
    instance->Initialize(resource, std::move(animator), renderContext_);

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

    // スケルトンがない場合はキーフレームモデルとして作成
    if (!resource->GetSkeleton()) {
        return CreateKeyframeModel(filePath, animationName, loop);
    }

    // アニメーションを取得
    std::string animName = animationName;
    if (animName.empty() && resource->HasAnimation()) {
        const auto& animations = resource->GetAnimations();
        if (!animations.empty()) {
            animName = animations.begin()->first;
        }
    }

    const Animation* animation = resource->GetAnimation(animName);
    if (!animation) {
        // アニメーションが見つからない場合は静的モデルとして作成
        auto instance = std::make_unique<Model>();
        instance->Initialize(resource, renderContext_);
        return instance;
    }

    // SkeletonAnimatorFactory を使って初期アニメーションを生成
    auto factory = std::make_unique<SkeletonAnimatorFactory>();
    auto skeletonAnimator = factory->CreateSkeletonAnimator(*resource->GetSkeleton(), *animation, loop);

    // インスタンスを作成し、ファクトリーを注入（SwitchAnimation で使用）
    auto instance = std::make_unique<Model>();
    instance->Initialize(resource, std::move(skeletonAnimator), renderContext_);
    instance->SetAnimationControllerFactory(std::make_unique<SkeletonAnimatorFactory>());

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

    Animation animation = AnimationLoader::LoadAnimationFile(resolvedDirectory, animFile);
    resource->AddAnimation(loadInfo.animationName, animation);
    return true;
}

void ModelManager::ClearCache()
{
    std::lock_guard<std::mutex> lock(cacheMutex_);
    resourceCache_.clear();
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

        // ロード権を確保して抜ける
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
    } catch (...) {
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
    if (filePaths.empty()) return;

    EnsureThreadPool();

    std::vector<std::future<void>> futures;
    futures.reserve(filePaths.size());

    for (const auto& path : filePaths) {
        futures.push_back(threadPool_->Submit([this, path]() {
            std::string resolved = ResolveFilePath(path);
            std::string dir, file;
            SplitPath(resolved, dir, file);
            LoadModelResourceInternal(dir, file);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }
}

void ModelManager::EnsureThreadPool()
{
    if (!threadPool_) {
        const uint32_t count = (std::max)(1u, std::thread::hardware_concurrency() / 2);
        threadPool_ = std::make_unique<ThreadPool>(count);
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

    // パスを正規化
    std::filesystem::path path(fullPath);
    std::string normalized = path.lexically_normal().string();
    
    // バックスラッシュをスラッシュに統一
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    return normalized;
}

void ModelManager::SplitPath(const std::string& filePath, std::string& outDirectory, std::string& outFilename) const
{
    std::filesystem::path path(filePath);
    outDirectory = path.parent_path().string();
    outFilename = path.filename().string();
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
    std::filesystem::path inputPath(normalized);
    std::string searchName = inputPath.filename().string();
    if (searchName.empty()) {
        searchName = normalized;
    }

    auto& assetDB = AssetDatabase::GetInstance();
    std::string assetPath = assetDB.FindAssetPath(searchName);
    if (assetPath.empty() && inputPath.has_stem()) {
        assetPath = assetDB.FindAssetPath(inputPath.stem().string());
    }
    if (!assetPath.empty()) {
        return assetPath;
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
}
