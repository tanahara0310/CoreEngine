#include "pch.h"
#include "MeshRendererComponent.h"

#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetInfo.h"
#include "Graphics/Material/MaterialInstance.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Pipeline/CustomShaderPipelineCache.h"
#include "Graphics/Render/Culling/ModelVisibility.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

#include <algorithm>
#include <iterator>
#include <utility>

COMPONENT_REGISTER(CoreEngine::MeshRendererComponent)

namespace
{
    /// @brief ブレンドの名前（`BlendMode` の並び）
    constexpr const char* kBlendModeNames[] = { "なし", "アルファ", "加算", "減算", "乗算", "スクリーン" };
    static_assert(std::size(kBlendModeNames) == CoreEngine::kBlendModeCount);

    /// @brief モデルファイルを指していないときにモデルの欄へ出す文字（プリミティブならその形）
    std::string DescribeUnsetModel(const void* instance)
    {
        const std::string name = static_cast<const CoreEngine::MeshRendererComponent*>(instance)->GetPrimitiveName();
        return name.empty() ? std::string{} : name + "（プリミティブ）";
    }
}

REFLECT_DEFINE_BEGIN(CoreEngine::MeshRendererComponent, "メッシュ描画")
    REFLECT_PARTIAL()
    REFLECT_ACCESSOR("model", "モデル", GetModelAsset, SetModelAsset,
        p.assetType = ::CoreEngine::AssetType::Model, p.emptyText = &DescribeUnsetModel)
    REFLECT_ACCESSOR("texture", "テクスチャ", GetTextureAsset, SetTextureAsset,
        p.assetType = ::CoreEngine::AssetType::Texture)
    REFLECT_ENUM_ACCESSOR("blendMode", "ブレンド", GetBlendMode, SetBlendMode, kBlendModeNames)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::MeshRendererComponent)

namespace CoreEngine
{
    MeshRendererComponent::MeshRendererComponent(std::string modelPath)
        : modelPath_(std::move(modelPath)), source_(Source::ModelFile)
    {
        modelAsset_.SetValue(Reflection::AssetRefValue{ {}, modelPath_ });
    }

    MeshRendererComponent::~MeshRendererComponent() = default;

    void MeshRendererComponent::SetModelFile(std::string modelPath)
    {
        modelPath_ = std::move(modelPath);
        source_ = Source::ModelFile;
        modelAsset_.SetValue(Reflection::AssetRefValue{ {}, modelPath_ });
    }

    void MeshRendererComponent::SetSkinnedModelFile(std::string modelPath, std::string initialClipName)
    {
        modelPath_ = std::move(modelPath);
        initialClipName_ = std::move(initialClipName);
        source_ = Source::SkinnedModelFile;
        modelAsset_.SetValue(Reflection::AssetRefValue{ {}, modelPath_ });
    }

    void MeshRendererComponent::SetPrimitive(std::unique_ptr<IPrimitiveMeshGenerator> generator)
    {
        generator_ = std::move(generator);
        source_ = Source::Primitive;
        modelAsset_.Reset();
    }

    void MeshRendererComponent::SetModelAsset(const Reflection::AssetRefValue& value)
    {
        if (value == modelAsset_.GetValue()) {
            return;
        }

        const bool fromFile = source_ == Source::ModelFile || source_ == Source::SkinnedModelFile;
        const std::string previous = modelAsset_.GetPath();
        modelAsset_.SetValue(value);

        // 何も指さなくなったら、ファイルから作ったメッシュだけを外す
        if (!modelAsset_.IsSet()) {
            if (fromFile) {
                modelPath_.clear();
                source_ = Source::None;
                model_.reset();
            }
            return;
        }

        // 引けないファイルとモデル以外は読み込まない（読み込み完了時の検証が警告する）
        const AssetInfo* info = ResolveAssetRef(modelAsset_.GetValue());
        if (!info || info->type != AssetType::Model) {
            return;
        }

        // 同じファイルを指し直しただけなら読み込み直さない
        const std::string next = ToAssetPath(*info);
        if (fromFile && next == previous) {
            return;
        }

        modelPath_ = next;
        if (source_ != Source::SkinnedModelFile) {
            source_ = Source::ModelFile;
        }
        if (awoken_) {
            ReloadFromSpec();
        }
    }

    std::string MeshRendererComponent::GetPrimitiveName() const
    {
        if (source_ != Source::Primitive || !generator_) {
            return {};
        }
        return generator_->GetDisplayName();
    }

    void MeshRendererComponent::SetTexture(std::string texturePath)
    {
        textureAsset_.SetPath(texturePath);
        LoadTexture(textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string{});
    }

    void MeshRendererComponent::SetTextureAsset(const Reflection::AssetRefValue& value)
    {
        if (value == textureAsset_.GetValue()) {
            return;
        }
        textureAsset_.SetValue(value);
        LoadTexture(textureAsset_.IsSet() ? textureAsset_.GetPath() : std::string{});
    }

    void MeshRendererComponent::LoadTexture(std::string texturePath)
    {
        if (texturePath.empty()) {
            texture_ = {};
            textureName_.clear();
            pendingTexturePath_.clear();
            return;
        }
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
        // 頂点をスケルトンで変形するモデルはスキニング経路（頂点変形を CS が行う）へ
        if (model_) {
            return model_->HasSkinCluster() ? RenderPassType::SkinnedModel : RenderPassType::Model;
        }
        return (source_ == Source::SkinnedModelFile)
            ? RenderPassType::SkinnedModel
            : RenderPassType::Model;
    }

    json MeshRendererComponent::OnSerialize() const
    {
        json j = json::object();
        if (!model_) {
            if (pendingMaterials_.is_array()) {
                j["materials"] = pendingMaterials_;
            }
            return j;
        }

        const ModelResource* resource = model_->GetModelResource();
        json materials = json::array();
        bool differs = false;
        for (size_t i = 0; i < model_->GetMaterialCount(); ++i) {
            const MaterialInstance* material = model_->GetMaterial(i);
            if (!material) {
                continue;
            }
            json value = material->ToJson();
            const MaterialInstance* defaults =
                resource ? resource->GetDefaultMaterial(static_cast<uint32_t>(i)) : nullptr;
            if (!defaults || defaults->ToJson() != value) {
                differs = true;
            }
            materials.push_back(std::move(value));
        }
        if (differs) {
            j["materials"] = std::move(materials);
        }
        return j;
    }

    void MeshRendererComponent::OnDeserialize(const json& j)
    {
        if (!j.is_object()) {
            return;
        }

        if (const auto it = j.find("materials"); it != j.end() && it->is_array()) {
            pendingMaterials_ = *it;
            ApplyPendingMaterials();
        }
    }

    void MeshRendererComponent::ApplyPendingMaterials()
    {
        if (!model_ || !pendingMaterials_.is_array()) {
            return;
        }

        // モデルの既定と同じスロットは、モデル間で共有する既定のマテリアルのままにする
        const ModelResource* resource = model_->GetModelResource();
        const size_t count = (std::min)(pendingMaterials_.size(), static_cast<size_t>(model_->GetMaterialCount()));
        for (size_t i = 0; i < count; ++i) {
            const MaterialInstance* defaults =
                resource ? resource->GetDefaultMaterial(static_cast<uint32_t>(i)) : nullptr;
            if (defaults && defaults->ToJson() == pendingMaterials_[i]) {
                continue;
            }
            if (MaterialInstance* material = model_->GetMaterial(i)) {
                material->FromJson(pendingMaterials_[i]);
            }
        }
        pendingMaterials_ = json();
    }

    void MeshRendererComponent::Awake()
    {
        awoken_ = true;

        // トランスフォームは描画に必須なので、無ければ自動で足す（Unity の RequireComponent 相当）
        if (GameObject* owner = GetOwner()) {
            transform_ = owner->GetOrAddComponent<TransformComponent>();
        }

        LoadMesh();
        ApplyPendingMaterials();

        if (!pendingTexturePath_.empty()) {
            LoadTexture(std::exchange(pendingTexturePath_, {}));
        }

        BuildCustomShaderPipelineIfNeeded();
    }

    // 指定が変わったときに、メッシュとマテリアルを作り直す
    void MeshRendererComponent::ReloadFromSpec()
    {
        if (source_ == Source::None) { return; }

        LoadMesh();
        ApplyPendingMaterials();

        if (!pendingTexturePath_.empty()) {
            LoadTexture(std::exchange(pendingTexturePath_, {}));
        }

        BuildCustomShaderPipelineIfNeeded();
    }

    // 指定（モデルファイル / スキニング / プリミティブ）に応じて実体を作る
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

    // カスタムシェーダー指定があるときだけ専用パイプラインを組む（共有キャッシュ経由）
    void MeshRendererComponent::BuildCustomShaderPipelineIfNeeded()
    {
        if (!customShaderProvider_ || !model_) { return; }

        GameObject* owner = GetOwner();
        EngineSystem* engine = owner ? owner->GetEngineSystem() : nullptr;
        auto* dxCommon = engine ? engine->GetService<GraphicsCore>() : nullptr;
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

    // ローカル AABB をワールドへ変換したもの（カリングの判定単位）
    BoundingBox MeshRendererComponent::GetWorldBoundingBox() const
    {
        const TransformComponent* transform = ResolveTransform();
        if (!model_ || !model_->GetModelResource() || !transform) {
            return BoundingBox();  // 無効な AABB
        }

        const BoundingBox& localAABB = model_->GetModelResource()->GetLocalBoundingBox();
        return localAABB.TransformBy(transform->Get().GetWorldMatrix());
    }

    // 視錐台 → Hi-Z の順に棄却してから Submit する。判定は DrawViewInfo だけで完結させる
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
