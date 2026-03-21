#include "ModelManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Animation/AnimationLoader.h"
#include "Animation/Animator.h"
#include "Skeleton/SkeletonAnimator.h"

#include <cassert>
#include <filesystem>
#include <algorithm>


namespace CoreEngine
{
void ModelManager::Initialize(DirectXCommon* dxCommon, ResourceFactory* factory)
{
    assert(dxCommon && factory);
    dxCommon_ = dxCommon;
    resourceFactory_ = factory;
    Model::Initialize(dxCommon);
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
    instance->Initialize(resource);

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
        instance->Initialize(resource);
        return instance;
    }

    // Animatorを作成
    auto animator = std::make_unique<Animator>();
    animator->SetAnimation(*animation);
    animator->SetLooping(loop);

    // インスタンスを作成
    auto instance = std::make_unique<Model>();
    instance->Initialize(resource, std::move(animator));

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
        instance->Initialize(resource);
        return instance;
    }

    // SkeletonAnimatorを作成（スケルトンをコピーして渡す）
    auto skeletonAnimator = std::make_unique<SkeletonAnimator>(*resource->GetSkeleton(), *animation);
    skeletonAnimator->SetLooping(loop);

    // インスタンスを作成
    auto instance = std::make_unique<Model>();
    instance->Initialize(resource, std::move(skeletonAnimator));

    return instance;
}

bool ModelManager::LoadAnimation(const AnimationLoadInfo& loadInfo)
{
    // modelFile をファイル名として直接 ResolveFilePath に渡す。
    // AssetDatabase がファイル名からディレクトリを含むフルパスを解決する。
    std::string resolvedModelPath = ResolveFilePath(loadInfo.modelFile);

    std::string resolvedDirectory, resolvedFilename;
    SplitPath(resolvedModelPath, resolvedDirectory, resolvedFilename);

    // モデルリソースを取得（キャッシュになければ読み込む）
    std::string normalizedModelPath = MakeNormalizedPath(resolvedDirectory, resolvedFilename);

    auto it = resourceCache_.find(normalizedModelPath);
    if (it == resourceCache_.end()) {
        LoadModelResourceInternal(resolvedDirectory, resolvedFilename);
        it = resourceCache_.find(normalizedModelPath);
        if (it == resourceCache_.end()) {
            return false;
        }
    }

    ModelResource* resource = it->second.get();

    // animationFile が未指定の場合はモデルファイルと同じ
    const std::string& animFile = loadInfo.animationFile.empty()
        ? resolvedFilename
        : loadInfo.animationFile;

    // アニメーションを読み込み（解決済みのディレクトリを使用）
    Animation animation = AnimationLoader::LoadAnimationFile(resolvedDirectory, animFile);

    resource->AddAnimation(loadInfo.animationName, animation);
    return true;
}

void ModelManager::ClearCache()
{
    resourceCache_.clear();
}

void ModelManager::LoadModelResource(const std::string& filePath)
{
    std::string resolvedPath = ResolveFilePath(filePath);
    std::string resolvedDirectory, resolvedFilename;
    SplitPath(resolvedPath, resolvedDirectory, resolvedFilename);
    LoadModelResourceInternal(resolvedDirectory, resolvedFilename);
}

ModelResource* ModelManager::LoadModelResourceInternal(const std::string& directoryPath, const std::string& filename)
{
    assert(IsInitialized());

    // 正規化されたパスをキャッシュキーとする
    std::string normalizedPath = MakeNormalizedPath(directoryPath, filename);

    // キャッシュに存在するか確認
    auto it = resourceCache_.find(normalizedPath);
    if (it != resourceCache_.end()) {
        return it->second.get();
    }

    // キャッシュミス - 新規読み込み
    auto resource = std::make_unique<ModelResource>();

    auto& textureManager = TextureManager::GetInstance();
    resource->Initialize(dxCommon_, resourceFactory_, &textureManager);
    resource->LoadFromFile(directoryPath, filename);

    // キャッシュに登録
    ModelResource* resourcePtr = resource.get();
    resourceCache_[normalizedPath] = std::move(resource);

    return resourcePtr;
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
    // パスを解決
    std::string resolvedPath = ResolveFilePath(filePath);

    std::string directoryPath, filename;
    SplitPath(resolvedPath, directoryPath, filename);
    
    std::string normalizedPath = MakeNormalizedPath(directoryPath, filename);
    
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
