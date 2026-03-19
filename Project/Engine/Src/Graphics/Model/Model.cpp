#include "Model.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Camera/ICamera.h"
#include "Graphics/Render/Model/ModelRenderer.h"
#include "Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Graphics/Render/Shadow/ShadowMapRenderer.h"
#include "Graphics/Model/Skeleton/SkeletonAnimator.h"
#include "Graphics/Model/Skeleton/SkinClusterGenerator.h"
#include "Graphics/Model/Animation/AnimationBlender.h"
#include "Utility/Logger/Logger.h"
#include "Math/MathCore.h"

#include <cassert>


namespace CoreEngine
{
    namespace {
        DirectXCommon* sDxCommon_ = nullptr;
        ShadowMapManager* sShadowMapManager_ = nullptr;
        ModelRenderer* sModelRenderer_ = nullptr;
        SkinnedModelRenderer* sSkinnedModelRenderer_ = nullptr;
        ShadowMapRenderer* sShadowMapRenderer_ = nullptr;
        uint32_t sCurrentTransformBufferIndex_ = static_cast<uint32_t>(Model::TransformBufferSlot::Game);

        /// @brief PBRテクスチャを描画コマンドにバインドする共通処理
        /// @tparam TIndexGetter ルートパラメータインデックス取得関数
        template<typename TIndexGetter>
        void BindPBRTextures(
            ID3D12GraphicsCommandList* cmdList,
            TIndexGetter&& getIndex,
            D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
            D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture)
        {
            int normalMapIdx = getIndex("gNormalMap");
            if (normalTexture.ptr != 0 && normalMapIdx >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(normalMapIdx, normalTexture);
            }

            if (metallicRoughnessTexture.ptr != 0) {
                int metallicIdx = getIndex("gMetallicMap");
                int roughnessIdx = getIndex("gRoughnessMap");
                if (metallicIdx >= 0) {
                    cmdList->SetGraphicsRootDescriptorTable(metallicIdx, metallicRoughnessTexture);
                }
                if (roughnessIdx >= 0) {
                    cmdList->SetGraphicsRootDescriptorTable(roughnessIdx, metallicRoughnessTexture);
                }
            }

            int aoIdx = getIndex("gAOMap");
            if (occlusionTexture.ptr != 0 && aoIdx >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(aoIdx, occlusionTexture);
            }
        }
    }

    void Model::Initialize(DirectXCommon* dxCommon) {
        assert(dxCommon);
        sDxCommon_ = dxCommon;
    }

    void Model::SetShadowMapManager(ShadowMapManager* shadowMapManager) {
        sShadowMapManager_ = shadowMapManager;
    }

    void Model::SetModelRenderer(ModelRenderer* modelRenderer) {
        sModelRenderer_ = modelRenderer;
    }

    void Model::SetSkinnedModelRenderer(SkinnedModelRenderer* skinnedModelRenderer) {
        sSkinnedModelRenderer_ = skinnedModelRenderer;
    }

    void Model::SetShadowMapRenderer(ShadowMapRenderer* shadowMapRenderer) {
        sShadowMapRenderer_ = shadowMapRenderer;
    }

    bool Model::IsEnvironmentMapAvailable() {
        return sModelRenderer_ != nullptr && sModelRenderer_->HasEnvironmentMap();
    }

    bool Model::IsIBLAvailable() {
        return sModelRenderer_ != nullptr && sModelRenderer_->HasIBLMaps();
    }

    void Model::SetTransformBufferSlot(TransformBufferSlot slot)
    {
        sCurrentTransformBufferIndex_ = static_cast<uint32_t>(slot);
    }

    void Model::Initialize(ModelResource* resource) {
        assert(resource && resource->IsLoaded());
        resource_ = resource;

        // MaterialInstanceを作成
        materialInstance_ = std::make_unique<MaterialInstance>();
        materialInstance_->Initialize(sDxCommon_->GetDevice());

        // モデルに埋め込まれた PBR テクスチャに基づきマテリアルフラグを自動設定
        const auto& subMeshes = resource_->GetSubMeshes();
        if (!subMeshes.empty()) {
            const auto& textures = resource_->GetMaterialTextures(subMeshes[0].materialIndex);
            bool hasPBRTextures = false;

            if (textures.hasNormal) {
                materialInstance_->SetNormalMapEnabled(true);
            }
            if (textures.hasMetallicRoughness) {
                materialInstance_->SetMetallicMapEnabled(true);
                materialInstance_->SetRoughnessMapEnabled(true);
                hasPBRTextures = true;
            }
            if (textures.hasOcclusion) {
                materialInstance_->SetAOMapEnabled(true);
                hasPBRTextures = true;
            }
            if (hasPBRTextures) {
                materialInstance_->SetPBREnabled(true);
            }
        }

        for (auto& wvpResource : wvpResources_) {
            wvpResource = ResourceFactory::CreateBufferResource(
                sDxCommon_->GetDevice(),
                sizeof(TransformationMatrix)
            );
        }

        // Skeletonをコピー
        if (resource_->GetSkeleton()) {
            skeleton_ = *resource_->GetSkeleton();

            const ModelData& modelData = resource_->GetModelData();
            if (!modelData.skinClusterData.empty()) {
                skinCluster_ = SkinClusterGenerator::CreateSkinCluster(
                    sDxCommon_->GetDevice(),
                    *skeleton_,
                    modelData,
                    sDxCommon_->GetDescriptorManager()
                );
            }
        }
    }

    void Model::Initialize(ModelResource* resource, std::unique_ptr<IAnimationController> controller) {
        // 基本の初期化を実行
        Initialize(resource);

        // アニメーションコントローラーを設定
        animationController_ = std::move(controller);
    }

    void Model::UpdateSkinCluster() {
        // SkinClusterとSkeletonが両方存在する場合のみ更新
        if (skinCluster_ && skeleton_) {
            SkinClusterGenerator::Update(*skinCluster_, *skeleton_);
        }
    }


    void Model::UpdateTransformationMatrix(const WorldTransform& transform, const ICamera* camera) {
        ID3D12Resource* transformBuffer = GetCurrentTransformBuffer();
        assert(transformBuffer);

        // 行列計算
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();
        Matrix4x4 viewMatrix = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
        Matrix4x4 worldViewProjectionMatrix = MathCore::Matrix::Multiply(
            worldMatrix,
            MathCore::Matrix::Multiply(viewMatrix, projectionMatrix)
        );

        // ShadowMapManagerからライトVP行列を取得
        Matrix4x4 lightVP = sShadowMapManager_ ?
            sShadowMapManager_->GetLightViewProjection() : MathCore::Matrix::Identity();

        // GPUメモリに書き込み
        TransformationMatrix* mappedData = nullptr;
        transformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        mappedData->world = worldMatrix;
        mappedData->WVP = worldViewProjectionMatrix;
        mappedData->worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mappedData->lightViewProjection = lightVP;
        transformBuffer->Unmap(0, nullptr);
    }

    void Model::Draw(const WorldTransform& transform, const ICamera* camera,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {

        assert(IsInitialized());
        assert(camera);

        ID3D12GraphicsCommandList* cmdList = sDxCommon_->GetCommandList();
        assert(cmdList);

        // WVP行列を更新（共通処理）
        UpdateTransformationMatrix(transform, camera);

        // サブメッシュごとに描画
        const auto& subMeshes = resource_->GetSubMeshes();
        assert(!subMeshes.empty() && "Model must have at least one submesh");

        for (const auto& subMesh : subMeshes) {
            // マテリアルのテクスチャを取得
            const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);

            // textureHandleが指定されている場合はBaseColorをオーバーライド
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTex = (textureHandle.ptr != 0)
                ? textureHandle : textures.baseColor;

            // スキンクラスターの有無で描画方法を自動判別
            if (HasSkinCluster()) {
                SetupSkinningDrawCommands(cmdList, baseColorTex, textures.normal,
                    textures.metallicRoughness, textures.occlusion);
            } else {
                SetupNormalDrawCommands(cmdList, baseColorTex, textures.normal,
                    textures.metallicRoughness, textures.occlusion);
            }

            // このサブメッシュの範囲を描画
            cmdList->DrawIndexedInstanced(subMesh.indexCount, 1, subMesh.startIndex, 0, 0);
        }
    }

    void Model::DrawShadow(const WorldTransform& transform, ID3D12GraphicsCommandList* cmdList) {
        assert(IsInitialized());
        assert(cmdList);

        ID3D12Resource* transformBuffer = GetTransformBuffer(TransformBufferSlot::Shadow);
        assert(transformBuffer);

        // ShadowMapManagerからライトVP行列を取得
        Matrix4x4 lightVP = sShadowMapManager_ ?
            sShadowMapManager_->GetLightViewProjection() : MathCore::Matrix::Identity();

        // シャドウマップ用のWVP行列を計算（ライトVP行列を使用）
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();
        Matrix4x4 lightWVP = MathCore::Matrix::Multiply(worldMatrix, lightVP);

        // GPUメモリに書き込み
        TransformationMatrix* mappedData = nullptr;
        transformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        mappedData->WVP = lightWVP;
        mappedData->world = worldMatrix;
        mappedData->worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mappedData->lightViewProjection = lightVP;
        transformBuffer->Unmap(0, nullptr);

        // 頂点バッファを設定
        cmdList->IASetVertexBuffers(0, 1, &resource_->vertexBufferView_);

        // インデックスバッファを設定
        cmdList->IASetIndexBuffer(&resource_->indexBufferView_);

        // WVP行列を設定（シェーダーリフレクションからインデックスを取得）
        int lightTransformIdx = sShadowMapRenderer_ ? sShadowMapRenderer_->GetRootParamIndex("gLightTransform") : 0;
        if (lightTransformIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(lightTransformIdx, transformBuffer->GetGPUVirtualAddress());
        }

        // スキニングモデルの場合はMatrixPaletteも設定
        if (HasSkinCluster()) {
            // スキニング用の頂点バッファも設定（slot 1）
            D3D12_VERTEX_BUFFER_VIEW skinningVBV = skinCluster_->influenceBufferView;
            cmdList->IASetVertexBuffers(1, 1, &skinningVBV);

            // MatrixPalette SRV（シェーダーリフレクションからインデックスを取得）
            int matrixPaletteIdx = sShadowMapRenderer_ ? sShadowMapRenderer_->GetRootParamIndex("gMatrixPalette") : 1;
            if (matrixPaletteIdx >= 0 && skinCluster_->paletteSrvHandle.second.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(matrixPaletteIdx, skinCluster_->paletteSrvHandle.second);
            }
#ifdef _DEBUG
            else if (matrixPaletteIdx >= 0 && skinCluster_->paletteSrvHandle.second.ptr == 0) {
                OutputDebugStringA("WARNING: Model::DrawShadow - MatrixPalette SRV is null for skinned model.\n");
            }
#endif
        }

        // 描画実行
        cmdList->DrawIndexedInstanced(resource_->indexCount_, 1, 0, 0, 0);
    }

    void Model::UpdateAnimation(float deltaTime) {
        if (!animationController_) return;

        // アニメーションの時間を進める
        animationController_->Update(deltaTime);

        // SkeletonAnimatorの場合は、スケルトンとスキンクラスターを同期
        if (auto* skeletonAnimator = dynamic_cast<SkeletonAnimator*>(animationController_.get())) {
            skeleton_ = skeletonAnimator->GetSkeleton();
            UpdateSkinCluster();
        }
        // AnimationBlenderの場合も、スケルトンとスキンクラスターを同期
        else if (auto* blender = dynamic_cast<AnimationBlender*>(animationController_.get())) {
            skeleton_ = blender->GetSkeleton();
            UpdateSkinCluster();
        }
    }

    void Model::ResetAnimation() {
        if (animationController_) {
            animationController_->Reset();
        }
    }

    float Model::GetAnimationTime() const {
        return animationController_ ? animationController_->GetAnimationTime() : 0.0f;
    }

    bool Model::IsAnimationFinished() const {
        return animationController_ ? animationController_->IsFinished() : true;
    }

    void Model::SetModelResource(ModelResource* resource)
    {
        resource_ = resource;
    }

    bool Model::SwitchAnimation(const std::string& animationName, bool loop) {

        // リソースがない場合は失敗
        if (!resource_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Cannot switch animation: ModelResource is null");
            return false;
        }

        // アニメーションを取得
        const Animation* newAnimation = resource_->GetAnimation(animationName);
        if (!newAnimation) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Animation not found: " + animationName);
            return false;
        }

        // SkeletonAnimatorの場合のみアニメーション切り替えが可能
        auto* skeletonAnimator = dynamic_cast<SkeletonAnimator*>(animationController_.get());
        if (!skeletonAnimator) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", "Animation switching is only supported for SkeletonAnimator");
            return false;
        }

        // 現在のスケルトン状態を保持したまま、新しいアニメーションコントローラーを作成
        Skeleton currentSkeleton = skeletonAnimator->GetSkeleton();
        animationController_ = std::make_unique<SkeletonAnimator>(currentSkeleton, *newAnimation, loop);

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Switched to animation: " + animationName);

        return true;
    }

    bool Model::SwitchAnimationWithBlend(const std::string& animationName, float blendDuration, bool loop) {
        // リソースがない場合は失敗
        if (!resource_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Cannot switch animation: ModelResource is null");
            return false;
        }

        // アニメーションを取得
        const Animation* newAnimation = resource_->GetAnimation(animationName);
        if (!newAnimation) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Animation not found: " + animationName);
            return false;
        }

        // AnimationBlenderとして動作している場合
        auto* blender = dynamic_cast<AnimationBlender*>(animationController_.get());
        if (blender) {
            // 現在のスケルトンを取得して新しいアニメーターを作成
            Skeleton currentSkeleton = blender->GetSkeleton();
            auto newAnimator = std::make_unique<SkeletonAnimator>(currentSkeleton, *newAnimation, loop);
            blender->StartBlend(std::move(newAnimator), blendDuration);

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Started blend to animation: " + animationName);
            return true;
        }

        // SkeletonAnimatorの場合
        auto* skeletonAnimator = dynamic_cast<SkeletonAnimator*>(animationController_.get());
        if (skeletonAnimator) {
            // 現在のアニメーターを保持
            Skeleton currentSkeleton = skeletonAnimator->GetSkeleton();
            auto currentAnimator = std::move(animationController_);

            // 新しいアニメーターを作成
            auto newAnimator = std::make_unique<SkeletonAnimator>(currentSkeleton, *newAnimation, loop);

            // Blenderを作成してブレンド開始
            auto blenderController = std::make_unique<AnimationBlender>(std::move(currentAnimator));
            blenderController->StartBlend(std::move(newAnimator), blendDuration);
            animationController_ = std::move(blenderController);

            Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Started blend to animation: " + animationName);
            return true;
        }

        Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", "Animation blending is only supported for SkeletonAnimator");
        return false;
    }

    // ===== PBRテクスチャ対応のSetup描画コマンド =====

    void Model::SetupNormalDrawCommands(ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture) {

        ID3D12Resource* transformBuffer = GetCurrentTransformBuffer();
        assert(transformBuffer);

        // 頂点バッファを設定
        cmdList->IASetVertexBuffers(0, 1, &resource_->vertexBufferView_);

        // インデックスバッファを設定
        cmdList->IASetIndexBuffer(&resource_->indexBufferView_);

        if (!sModelRenderer_) {
            throw std::runtime_error("ModelRenderer is not set. Call Model::SetModelRenderer first.");
        }

        const bool useGBufferBinding = sModelRenderer_->IsInGBufferPass();
        auto getModelRootIndex = [&](const std::string& name) {
            return useGBufferBinding
                ? sModelRenderer_->GetGBufferRootParamIndex(name)
                : sModelRenderer_->GetRootParamIndex(name);
        };

        const int materialIdx = getModelRootIndex("gMaterial");
        const int transformIdx = getModelRootIndex("gTransformationMatrix");
        const int textureIdx = getModelRootIndex("gTexture");

        if (materialIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(materialIdx, materialInstance_->GetGPUVirtualAddress());
        }

        if (transformIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(transformIdx, transformBuffer->GetGPUVirtualAddress());
        }

        if (textureIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(textureIdx, baseColorTexture);
        }

        BindPBRTextures(cmdList, getModelRootIndex, normalTexture, metallicRoughnessTexture, occlusionTexture);
    }

    void Model::SetupSkinningDrawCommands(ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture) {

        ID3D12Resource* transformBuffer = GetCurrentTransformBuffer();
        assert(transformBuffer);

        assert(skinCluster_.has_value());
        assert(sSkinnedModelRenderer_ != nullptr);

        const bool useGBufferBinding = sSkinnedModelRenderer_->IsInGBufferPass();
        auto getSkinnedRootIndex = [&](const std::string& name) {
            return useGBufferBinding
                ? sSkinnedModelRenderer_->GetGBufferRootParamIndex(name)
                : sSkinnedModelRenderer_->GetRootParamIndex(name);
        };

        // 頂点バッファを2つ設定（通常の頂点データとInfluenceデータ）
        D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
            resource_->vertexBufferView_,      // Slot 0: VertexData
            skinCluster_->influenceBufferView  // Slot 1: VertexInfluence
        };
        cmdList->IASetVertexBuffers(0, 2, vbvs);

        // インデックスバッファを設定
        cmdList->IASetIndexBuffer(&resource_->indexBufferView_);

        // WVP行列を設定
        const int transformIdx = getSkinnedRootIndex("gTransformationMatrix");
        if (transformIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(transformIdx, transformBuffer->GetGPUVirtualAddress());
        }

        // MatrixPaletteを設定
        const int matrixPaletteIdx = getSkinnedRootIndex("gMatrixPalette");
        if (matrixPaletteIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(matrixPaletteIdx, skinCluster_->paletteSrvHandle.second);
        }

        // マテリアルを設定
        const int materialIdx = getSkinnedRootIndex("gMaterial");
        if (materialIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(materialIdx, materialInstance_->GetGPUVirtualAddress());
        }

        // BaseColorテクスチャを設定
        const int textureIdx = getSkinnedRootIndex("gTexture");
        if (textureIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(textureIdx, baseColorTexture);
        }

        BindPBRTextures(cmdList, getSkinnedRootIndex, normalTexture, metallicRoughnessTexture, occlusionTexture);
    }

    ID3D12Resource* Model::GetCurrentTransformBuffer() const
    {
        assert(sCurrentTransformBufferIndex_ < wvpResources_.size());
        return wvpResources_[sCurrentTransformBufferIndex_].Get();
    }

    ID3D12Resource* Model::GetTransformBuffer(TransformBufferSlot slot) const
    {
        const size_t index = static_cast<size_t>(slot);
        assert(index < wvpResources_.size());
        return wvpResources_[index].Get();
    }

    // ===== クエリ =====

    bool Model::IsInitialized() const {
        return resource_ != nullptr && materialInstance_ != nullptr;
    }

    const std::optional<Skeleton>& Model::GetSkeleton() const {
        return skeleton_;
    }

    bool Model::HasSkinCluster() const {
        return skinCluster_.has_value();
    }

    bool Model::HasAnimationController() const {
        return animationController_ != nullptr;
    }

    bool Model::HasNormalMap() const {
        if (!resource_ || resource_->GetSubMeshes().empty()) return false;
        return resource_->GetMaterialTextures(resource_->GetSubMeshes()[0].materialIndex).hasNormal;
    }

    bool Model::HasMetallicRoughnessMap() const {
        if (!resource_ || resource_->GetSubMeshes().empty()) return false;
        return resource_->GetMaterialTextures(resource_->GetSubMeshes()[0].materialIndex).hasMetallicRoughness;
    }

    bool Model::HasOcclusionMap() const {
        if (!resource_ || resource_->GetSubMeshes().empty()) return false;
        return resource_->GetMaterialTextures(resource_->GetSubMeshes()[0].materialIndex).hasOcclusion;
    }

    Model::RenderType Model::GetRenderType() const {
        return HasSkinCluster() ? RenderType::Skinning : RenderType::Normal;
    }

    ModelResource* Model::GetModelResource() {
        return resource_;
    }

    const ModelResource* Model::GetModelResource() const {
        return resource_;
    }

}


