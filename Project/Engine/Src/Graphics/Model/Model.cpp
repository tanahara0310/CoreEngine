#include "pch.h"
#include "Model.h"
#include "ModelRenderContext.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Camera/ICamera.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Model/Instancing/InstanceBatchManager.h"
#include "Graphics/Render/Shadow/ShadowMapRenderer.h"
#include "Graphics/Model/Skeleton/SkinClusterGenerator.h"
#include "Graphics/Model/Skeleton/SkinningComputeDispatcher.h"
#include "Utility/Logger/Logger.h"
#include "Math/MathCore.h"

#include <cassert>


namespace CoreEngine
{

    bool Model::IsIBLAvailable() const {
        // 自身の renderContext_ 経由でレンダラーの IBL テクスチャ状態を確認
        return renderContext_.modelRenderer != nullptr && renderContext_.modelRenderer->HasIBLMaps();
    }

    void Model::Initialize(ModelResource* resource, const ModelRenderContext& ctx) {
        assert(resource && resource->IsLoaded());
        assert(ctx.IsComplete() && "ModelRenderContext must be fully initialized (use ModelManager to create models)");
        resource_ = resource;
        renderContext_ = ctx;

        // マテリアルスロット数分の MaterialInstance を作成し、
        // アセット側の PBR ファクターとテクスチャ有無を各スロットへ反映する。
        const auto& materials = resource_->GetMaterials();
        const size_t materialCount = (std::max<size_t>)(materials.size(), 1);
        materialInstances_.clear();
        materialInstances_.reserve(materialCount);
        for (size_t i = 0; i < materialCount; ++i) {
            auto instance = std::make_unique<MaterialInstance>();
            instance->Initialize(renderContext_.dxCommon->GetDevice());

            if (i < materials.size()) {
                const MaterialAsset& asset = materials[i];
                instance->SetColor(asset.baseColorFactor);
                instance->SetMetallic(asset.metallicFactor);
                instance->SetRoughness(asset.roughnessFactor);
                instance->SetEmissiveFactor(asset.emissiveFactor);
                instance->SetAlphaCutoff(asset.alphaCutoff);
            }

            // 法線マップのみフラグ制御（法線はファクター乗算で無効化できないため）
            const auto& textures = resource_->GetMaterialTextures(static_cast<uint32_t>(i));
            instance->SetNormalMapEnabled(textures.hasNormal);

            materialInstances_.push_back(std::move(instance));
        }

        for (auto& wvpResource : wvpResources_) {
            wvpResource = ResourceFactory::CreateBufferResource(
                renderContext_.dxCommon->GetDevice(),
                sizeof(TransformationMatrix)
            );
        }

        // スケルトンを持つモデルは SkinCluster を作成する
        // （スケルトンの実体はリソースまたはアニメーターが所有し、Model はコピーを持たない）
        if (resource_->GetSkeleton()) {
            const ModelData& modelData = resource_->GetModelData();
            if (!modelData.skinClusterData.empty()) {
                skinCluster_ = SkinClusterGenerator::CreateSkinCluster(
                    renderContext_.dxCommon->GetDevice(),
                    *resource_->GetSkeleton(),
                    modelData,
                    renderContext_.dxCommon->GetDescriptorManager(),
                    resource_->GetVertexBuffer(),
                    resource_->GetVertexCount()
                );
            }
        }
    }

    void Model::SetAnimationPlayer(std::unique_ptr<AnimationPlayer> player) {
        animationPlayer_ = std::move(player);
    }

    void Model::UpdateSkinCluster(const Skeleton& skeleton) {
        if (skinCluster_) {
            SkinClusterGenerator::Update(*skinCluster_, skeleton);
        }
    }

    void Model::EnsureGPUSkinning(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* restorePSO) {
        if (!skinCluster_ || !skinCluster_->needsGPUSkinning) {
            return;
        }
        assert(renderContext_.skinningDispatcher);

        renderContext_.skinningDispatcher->Dispatch(
            cmdList,
            renderContext_.dxCommon->GetSRVHeap(),
            *skinCluster_,
            resource_->GetVertexCount());
        skinCluster_->needsGPUSkinning = false;

        // Dispatch がPSOスロットを書き換えるため、呼び出し元パスのグラフィックスPSOへ戻す
        if (restorePSO) {
            cmdList->SetPipelineState(restorePSO);
        }
    }


    void Model::UpdateTransformationMatrix(const WorldTransform& transform, const ICamera* camera,
        TransformBufferSlot slot)
    {
        ID3D12Resource* transformBuffer = GetTransformBuffer(slot);
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
        Matrix4x4 lightVP = renderContext_.shadowMapManager ?
            renderContext_.shadowMapManager->GetLightViewProjection() : MathCore::Matrix::Identity();

        size_t slotIdx = static_cast<size_t>(slot);

        // GPUメモリに書き込み
        TransformationMatrix* mappedData = nullptr;
        transformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        mappedData->world = worldMatrix;
        // 初回フレームは prevWVP = currentWVP にしてモーションベクター=0を保証する
        mappedData->prevWVP = prevWVPInitialized_[slotIdx] ? prevWVP_[slotIdx] : worldViewProjectionMatrix;
        mappedData->WVP = worldViewProjectionMatrix;
        mappedData->worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mappedData->lightViewProjection = lightVP;
        transformBuffer->Unmap(0, nullptr);

        // 今フレームのWVPを次フレームの prevWVP として保存
        prevWVP_[slotIdx] = worldViewProjectionMatrix;
        prevWVPInitialized_[slotIdx] = true;
    }

    void Model::Draw(const WorldTransform& transform, const ICamera* camera,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle, TransformBufferSlot slot) {

        assert(IsInitialized());
        assert(camera);

        // 呼び出し側がスロットを明示しない場合（デフォルト Game）は
        // BaseScene が SetCurrentRenderSlot() で設定したグローバルスロットを使用する。
        if (slot == TransformBufferSlot::Game) {
            slot = s_currentRenderSlot_;
        }

        ID3D12GraphicsCommandList* cmdList = renderContext_.dxCommon->GetCommandList();
        assert(cmdList);

        const auto& subMeshes = resource_->GetSubMeshes();
        assert(!subMeshes.empty() && "Model must have at least one submesh");

        const bool isSkinned = HasSkinCluster();

        if (isSkinned) {
            // スキニングモデルは従来通り CBV 経由で即時描画する
            UpdateTransformationMatrix(transform, camera, slot);

            BaseModelRenderer* renderer = renderContext_.skinnedRenderer;
            assert(renderer);

            EnsureGPUSkinning(cmdList, renderer->GetCurrentPipelineState());

            for (const auto& subMesh : subMeshes) {
                const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);
                D3D12_GPU_DESCRIPTOR_HANDLE baseColorTex = (textureHandle.ptr != 0)
                    ? textureHandle : textures.baseColor;

                ModelDrawPacket packet = BuildSkinningDrawPacket(
                    subMesh, baseColorTex, textures.normal,
                    textures.metallicRoughness, textures.occlusion, textures.emissive, slot);

                renderer->BindModelDrawPacket(cmdList, packet);
            }
            return;
        }

        // 通常モデル: インスタンシングバッチに行列を Submit する（即時描画はしない）
        InstanceBatchManager* batch = renderContext_.instanceBatchManager;
        assert(batch);

        // 行列計算
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();
        Matrix4x4 viewMatrix = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
        Matrix4x4 wvp = MathCore::Matrix::Multiply(
            worldMatrix,
            MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
        Matrix4x4 lightVP = renderContext_.shadowMapManager
            ? renderContext_.shadowMapManager->GetLightViewProjection()
            : MathCore::Matrix::Identity();

        const size_t slotIdx = static_cast<size_t>(slot);
        TransformationMatrix mtx{};
        mtx.world = worldMatrix;
        mtx.WVP = wvp;
        mtx.prevWVP = prevWVPInitialized_[slotIdx] ? prevWVP_[slotIdx] : wvp;
        mtx.worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mtx.lightViewProjection = lightVP;
        prevWVP_[slotIdx] = wvp;
        prevWVPInitialized_[slotIdx] = true;

        // パスの種別はレンダラーのフレームコンテキストから判定する
        const bool isGBufferPass = renderContext_.modelRenderer->IsInGBufferPass();

        for (uint32_t i = 0; i < subMeshes.size(); ++i) {
            const auto& subMesh = subMeshes[i];
            const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);
            // マテリアルCBVはサブメッシュのマテリアルスロットに対応するインスタンスを使用
            const D3D12_GPU_VIRTUAL_ADDRESS materialCBV =
                MaterialForSlot(subMesh.materialIndex)->GetGPUVirtualAddress();
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTex = (textureHandle.ptr != 0)
                ? textureHandle : textures.baseColor;

            InstanceBatchKey key{};
            key.resource = resource_;
            key.subMeshIndex = i;
            key.baseColorSRV = baseColorTex.ptr;
            key.normalMapSRV = textures.normal.ptr;
            key.metallicRoughnessSRV = textures.metallicRoughness.ptr;
            key.occlusionSRV = textures.occlusion.ptr;
            key.emissiveSRV = textures.emissive.ptr;
            key.materialCBV = static_cast<uint64_t>(materialCBV);
            key.isGBufferPass = isGBufferPass;
            key.customForwardPSO = isGBufferPass ? nullptr : customForwardPSO_;
            key.customRootSignature = isGBufferPass ? nullptr : customRootSignature_;
            key.customProvider = isGBufferPass ? nullptr : customProvider_;
            key.customPipeline = isGBufferPass ? nullptr : customPipeline_;

            batch->Submit(key, mtx, materialCBV);
        }
    }

    void Model::DrawShadow(const WorldTransform& transform, ID3D12GraphicsCommandList* cmdList) {
    assert(IsInitialized());
    assert(cmdList);

    ID3D12Resource* transformBuffer = GetTransformBuffer(TransformBufferSlot::Shadow);
    assert(transformBuffer);

    // ShadowMapManagerからライトVP行列を取得
    Matrix4x4 lightVP = renderContext_.shadowMapManager ?
        renderContext_.shadowMapManager->GetLightViewProjection() : MathCore::Matrix::Identity();

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

    // スキニングモデルの場合は描画前にGPUスキニング(CS)を実行し、結果の頂点バッファを使う
    if (HasSkinCluster()) {
        EnsureGPUSkinning(cmdList, renderContext_.shadowRenderer ? renderContext_.shadowRenderer->GetCurrentPipelineState() : nullptr);
        cmdList->IASetVertexBuffers(0, 1, &skinCluster_->outputVertexBufferView);
    } else {
        cmdList->IASetVertexBuffers(0, 1, &resource_->GetVertexBufferView());
    }

    // インデックスバッファを設定
    cmdList->IASetIndexBuffer(&resource_->GetIndexBufferView());

    // WVP行列を設定（シェーダーリフレクションからインデックスを取得）
    int lightTransformIdx = renderContext_.shadowRenderer ? renderContext_.shadowRenderer->GetRootParamIndex("gLightTransform") : 0;
    if (lightTransformIdx >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(lightTransformIdx, transformBuffer->GetGPUVirtualAddress());
    }

    // 描画実行
    cmdList->DrawIndexedInstanced(resource_->GetIndexCount(), 1, 0, 0, 0);
}

void Model::UpdateAnimation(float deltaTime) {
    if (!animationPlayer_) return;

    // アニメーション時刻を進める
    animationPlayer_->Update(deltaTime);

    // スケルトンの姿勢を SkinCluster のマトリックスパレットへ反映する（コピーなし・参照渡し）
    if (const Skeleton* skel = animationPlayer_->GetSkeleton()) {
        UpdateSkinCluster(*skel);
    }
}

// ===== ModelDrawPacket 組み立て =====

ModelDrawPacket Model::BuildNormalDrawPacket(
    const SubMeshData& subMesh,
    D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE emissiveTexture,
    TransformBufferSlot slot) const
{
    ID3D12Resource* transformBuffer = GetTransformBuffer(slot);
    assert(transformBuffer);
    assert(renderContext_.modelRenderer);

    ModelDrawPacket packet;
    packet.vertexBufferViews[0] = resource_->GetVertexBufferView();
    packet.vertexBufferViewCount = 1;
    packet.indexBufferView = resource_->GetIndexBufferView();
    packet.indexCount = subMesh.indexCount;
    packet.startIndex = subMesh.startIndex;
    packet.instanceDataSRV = transformBuffer->GetGPUVirtualAddress();
    packet.instanceCount = 1;
    packet.materialCBV = MaterialForSlot(subMesh.materialIndex)->GetGPUVirtualAddress();
    packet.baseColorSRV = baseColorTexture;
    packet.normalMapSRV = normalTexture;
    packet.metallicRoughnessSRV = metallicRoughnessTexture;
    packet.occlusionSRV = occlusionTexture;
    packet.emissiveSRV = emissiveTexture;
    packet.isSkinned = false;
    return packet;
}

ModelDrawPacket Model::BuildSkinningDrawPacket(
    const SubMeshData& subMesh,
    D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE emissiveTexture,
    TransformBufferSlot slot) const
{
    ID3D12Resource* transformBuffer = GetTransformBuffer(slot);
    assert(transformBuffer);
    assert(skinCluster_.has_value());
    assert(renderContext_.skinnedRenderer);

    ModelDrawPacket packet;
    // GPUスキニング(CS)で計算済みの頂点バッファをそのまま使う（Influence/MatrixPaletteは描画時には不要）
    packet.vertexBufferViews[0] = skinCluster_->outputVertexBufferView;
    packet.vertexBufferViewCount = 1;
    packet.indexBufferView = resource_->GetIndexBufferView();
    packet.indexCount = subMesh.indexCount;
    packet.startIndex = subMesh.startIndex;
    packet.instanceDataSRV = transformBuffer->GetGPUVirtualAddress();
    packet.instanceCount = 1;
    packet.materialCBV = MaterialForSlot(subMesh.materialIndex)->GetGPUVirtualAddress();
    packet.baseColorSRV = baseColorTexture;
    packet.normalMapSRV = normalTexture;
    packet.metallicRoughnessSRV = metallicRoughnessTexture;
    packet.occlusionSRV = occlusionTexture;
    packet.emissiveSRV = emissiveTexture;
    packet.isSkinned = true;
    return packet;
}

ID3D12Resource* Model::GetTransformBuffer(TransformBufferSlot slot) const
{
    const size_t index = static_cast<size_t>(slot);
    assert(index < wvpResources_.size());
    return wvpResources_[index].Get();
}

// ===== クエリ =====

MaterialInstance* Model::MaterialForSlot(uint32_t materialIndex) const {
    assert(!materialInstances_.empty());
    const size_t index = materialIndex < materialInstances_.size() ? materialIndex : 0;
    return materialInstances_[index].get();
}

bool Model::IsInitialized() const {
    return resource_ != nullptr && !materialInstances_.empty();
}

bool Model::HasSkinCluster() const {
    return skinCluster_.has_value();
}

bool Model::HasNormalMap(size_t materialIndex) const {
    if (!resource_ || materialIndex >= resource_->GetMaterials().size()) return false;
    return resource_->GetMaterialTextures(static_cast<uint32_t>(materialIndex)).hasNormal;
}

bool Model::HasMetallicRoughnessMap(size_t materialIndex) const {
    if (!resource_ || materialIndex >= resource_->GetMaterials().size()) return false;
    return resource_->GetMaterialTextures(static_cast<uint32_t>(materialIndex)).hasMetallicRoughness;
}

bool Model::HasOcclusionMap(size_t materialIndex) const {
    if (!resource_ || materialIndex >= resource_->GetMaterials().size()) return false;
    return resource_->GetMaterialTextures(static_cast<uint32_t>(materialIndex)).hasOcclusion;
}

ModelResource* Model::GetModelResource() {
    return resource_;
}

const ModelResource* Model::GetModelResource() const {
    return resource_;
}

}


