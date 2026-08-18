#include "pch.h"
#include "MeshRendererComponent.h"

#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Pipeline/CustomShaderPipelineCache.h"
#include "Graphics/Render/Culling/ModelVisibility.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

namespace CoreEngine
{
    MeshRendererComponent::~MeshRendererComponent() = default;

    void MeshRendererComponent::SetModelFile(std::string modelPath)
    {
        modelPath_ = std::move(modelPath);
        source_ = Source::ModelFile;
    }

    void MeshRendererComponent::SetSkinnedModelFile(std::string modelPath, std::string initialClipName)
    {
        modelPath_ = std::move(modelPath);
        initialClipName_ = std::move(initialClipName);
        source_ = Source::SkinnedModelFile;
    }

    void MeshRendererComponent::SetPrimitive(std::unique_ptr<IPrimitiveMeshGenerator> generator)
    {
        generator_ = std::move(generator);
        source_ = Source::Primitive;
    }

    void MeshRendererComponent::SetTexture(std::string texturePath)
    {
        if (TextureManager::GetInstance().IsInitialized()) {
            texture_ = TextureManager::GetInstance().Load(texturePath);
            textureName_ = std::move(texturePath);
        } else {
            // まだテクスチャマネージャが立っていない（Awake より前）ので後回しにする
            pendingTexturePath_ = std::move(texturePath);
        }
    }

    RenderPassType MeshRendererComponent::GetRenderPassType() const
    {
        if (passTypeOverride_) {
            return *passTypeOverride_;
        }
        // スケルトン付きモデルはスキニング経路（頂点変形を CS が行う）へ
        return (source_ == Source::SkinnedModelFile)
            ? RenderPassType::SkinnedModel
            : RenderPassType::Model;
    }

    void MeshRendererComponent::Awake()
    {
        // トランスフォームは描画に必須なので、無ければ自動で足す（Unity の RequireComponent 相当）
        if (GameObject* owner = GetOwner()) {
            transform_ = owner->GetOrAddComponent<TransformComponent>();
        }

        LoadMesh();

        if (!pendingTexturePath_.empty()) {
            SetTexture(std::move(pendingTexturePath_));
            pendingTexturePath_.clear();
        }

        BuildCustomShaderPipelineIfNeeded();
    }

    void MeshRendererComponent::ReloadFromSpec()
    {
        if (source_ == Source::None) { return; }

        LoadMesh();

        if (!pendingTexturePath_.empty()) {
            SetTexture(std::move(pendingTexturePath_));
            pendingTexturePath_.clear();
        }

        BuildCustomShaderPipelineIfNeeded();
    }

    void MeshRendererComponent::LoadMesh()
    {
        if (source_ == Source::None) { return; }

        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* modelMgr = engine ? engine->GetService<ModelManager>() : nullptr;
        if (!modelMgr) { return; }

        switch (source_) {
        case Source::ModelFile:
            if (!modelPath_.empty()) {
                model_ = modelMgr->CreateStaticModel(modelPath_);
            }
            break;

        case Source::SkinnedModelFile:
            if (!modelPath_.empty()) {
                model_ = modelMgr->CreateSkeletonModel(modelPath_, initialClipName_, true);
            }
            break;

        case Source::Primitive:
            if (generator_) {
                model_ = modelMgr->CreatePrimitiveModel(generator_->GetCacheKey(), *generator_);
            }
            break;

        default:
            break;
        }
    }

    void MeshRendererComponent::BuildCustomShaderPipelineIfNeeded()
    {
        if (!customShaderProvider_ || !model_) { return; }

        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* dxCommon = engine ? engine->GetService<DirectXCommon>() : nullptr;
        auto* modelMgr = engine ? engine->GetService<ModelManager>() : nullptr;
        if (!dxCommon || !modelMgr) { return; }

        const ModelRenderContext& ctx = modelMgr->GetRenderContext();
        if (!ctx.IsValid()) { return; }

        // 同一シェーダー＋同一設定なら既存パイプラインを共有する
        // （同じカスタムシェーダーをN個配置してもコンパイル・PSO構築は1回）
        BaseModelRenderer* renderer = ctx.modelRenderer;
        customShaderPipeline_ = modelMgr->GetCustomShaderPipelineCache()->GetOrBuild(
            dxCommon->GetDevice(),
            *renderer->GetShaderCompiler(),
            *renderer->GetReflectionBuilder(),
            *customShaderProvider_);

        if (customShaderPipeline_ && customShaderPipeline_->HasForwardPSO()) {
            model_->SetCustomForwardPSO(customShaderPipeline_->GetForwardPSO(blendMode_));
            model_->SetCustomRootSignature(customShaderPipeline_->GetForwardRootSignature());
            model_->SetCustomPipeline(customShaderPipeline_.get());
            model_->SetCustomShaderProvider(customShaderProvider_);
        }
    }

    TransformComponent* MeshRendererComponent::ResolveTransform() const
    {
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
        }
        return transform_;
    }

    BoundingBox MeshRendererComponent::GetWorldBoundingBox() const
    {
        const TransformComponent* transform = ResolveTransform();
        if (!model_ || !model_->GetModelResource() || !transform) {
            return BoundingBox();  // 無効な AABB
        }

        const BoundingBox& localAABB = model_->GetModelResource()->GetLocalBoundingBox();
        return localAABB.TransformBy(transform->Get().GetWorldMatrix());
    }

    bool MeshRendererComponent::DrawIfVisible(const DrawViewInfo& view)
    {
        if (!model_ || !view.view || !view.view->isValid) {
            return false;
        }

        TransformComponent* transform = ResolveTransform();
        if (!transform) {
            return false;
        }

        // 視錐台カリング（判定内容とデバッグトグルは ModelVisibility が持つ）
        if (!ModelVisibility::IsModelInView(view.view->frustum, GetWorldBoundingBox())) {
            return false;
        }

        model_->Draw(transform->Get(), view, texture_.gpuHandle);
        return true;
    }
}
