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

#include <algorithm>
#include <cassert>
#include <cmath>


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

        // マテリアルスロット数分の枠だけ確保する（中身は Copy-on-Write で未確保のまま）。
        // オーバーライドが発生するまでは ModelResource 共有のデフォルトマテリアルを参照するため、
        // 同一モデルを複数配置してもマテリアルCBVアドレスが一致しインスタンシングバッチが統合される。
        const auto& materials = resource_->GetMaterials();
        const size_t materialCount = (std::max<size_t>)(materials.size(), 1);
        materialInstances_.clear();
        materialInstances_.resize(materialCount);

        // WVP バッファをフレーム数分確保する（CPU が複数フレーム先行して書き込んでも
        // GPU がまだ参照中のデータを上書きしないよう、役割ごとにリングバッファ化する）。
        for (auto& buffer : gameTransformBuffers_) {
            buffer = ResourceFactory::CreateBufferResource(
                renderContext_.dxCommon->GetDevice(),
                sizeof(TransformationMatrix)
            );
        }
        for (auto& buffer : shadowTransformBuffers_) {
            buffer = ResourceFactory::CreateBufferResource(
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


    void Model::UpdateTransformationMatrix(const WorldTransform& transform, const ICamera* camera)
    {
        ID3D12Resource* transformBuffer = GetGameTransformBuffer();
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

        // GPUメモリに書き込み
        TransformationMatrix* mappedData = nullptr;
        transformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        mappedData->world = worldMatrix;
        // 初回フレームは prevWVP = currentWVP にしてモーションベクター=0を保証する
        mappedData->prevWVP = prevGameWVPInitialized_ ? prevGameWVP_ : worldViewProjectionMatrix;
        mappedData->WVP = worldViewProjectionMatrix;
        mappedData->worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mappedData->lightViewProjection = lightVP;
        transformBuffer->Unmap(0, nullptr);

        // 今フレームのWVPを次フレームの prevWVP として保存
        prevGameWVP_ = worldViewProjectionMatrix;
        prevGameWVPInitialized_ = true;
    }

    void Model::Draw(const WorldTransform& transform, const ICamera* camera,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {

        assert(IsInitialized());
        assert(camera);

        ID3D12GraphicsCommandList* cmdList = renderContext_.dxCommon->GetCommandList();
        assert(cmdList);

        const auto& subMeshes = resource_->GetSubMeshes();
        assert(!subMeshes.empty() && "Model must have at least one submesh");

        const bool isSkinned = HasSkinCluster();

        if (isSkinned) {
            // スキニングモデルは従来通り CBV 経由で即時描画する
            UpdateTransformationMatrix(transform, camera);

            BaseModelRenderer* renderer = renderContext_.skinnedRenderer;
            assert(renderer);

            EnsureGPUSkinning(cmdList, renderer->GetCurrentPipelineState());

            for (const auto& subMesh : subMeshes) {
                const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);
                D3D12_GPU_DESCRIPTOR_HANDLE baseColorTex = (textureHandle.ptr != 0)
                    ? textureHandle : textures.baseColor;

                ModelDrawPacket packet = BuildSkinningDrawPacket(
                    subMesh, baseColorTex, textures.normal,
                    textures.metallicRoughness, textures.occlusion, textures.emissive);

                renderer->BindModelDrawPacket(cmdList, packet);
            }
            return;
        }

        // 通常モデル: インスタンシングバッチに行列を Submit する（即時描画はしない）
        InstanceBatchManager* batch = renderContext_.instanceBatchManager;
        assert(batch);

        // 行列計算
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();

        // ===== LOD 選択（AABB の画面投影サイズベース、詳細は ModelVisibility 側） =====
        const uint32_t lodIndex = ModelVisibility::SelectLod(*resource_, worldMatrix, camera);

        Matrix4x4 viewMatrix = camera->GetViewMatrix();
        Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();
        Matrix4x4 wvp = MathCore::Matrix::Multiply(
            worldMatrix,
            MathCore::Matrix::Multiply(viewMatrix, projectionMatrix));
        Matrix4x4 lightVP = renderContext_.shadowMapManager
            ? renderContext_.shadowMapManager->GetLightViewProjection()
            : MathCore::Matrix::Identity();

        TransformationMatrix mtx{};
        mtx.world = worldMatrix;
        mtx.WVP = wvp;
        mtx.prevWVP = prevGameWVPInitialized_ ? prevGameWVP_ : wvp;
        mtx.worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mtx.lightViewProjection = lightVP;
        prevGameWVP_ = wvp;
        prevGameWVPInitialized_ = true;

        // パスの種別はレンダラーのフレームコンテキストから判定する
        const bool isGBufferPass = renderContext_.modelRenderer->IsInGBufferPass();

        // ===== Hi-Z オクルージョンカリング（メイン GameView の GBuffer 構築時のみ） =====
        // サブメッシュ単位で前々フレームの遮蔽判定結果を参照し、遮蔽中の範囲だけ Submit を
        // スキップする。スロット管理・AABB登録などの詳細は ModelVisibility 側が持つ。
        // prevGameWVP_ は上で更新済みのため、再可視化フレームのモーションベクターは正しい。
        // シャドウは DrawShadow の別経路なので影響しない。
        visibility_.BeginOcclusionQuery(renderContext_.hiZOcclusion, *resource_, worldMatrix,
            MathCore::Matrix::Multiply(viewMatrix, projectionMatrix), isGBufferPass);

        for (uint32_t i = 0; i < subMeshes.size(); ++i) {
            const auto& subMesh = subMeshes[i];

            if (!visibility_.IsSubMeshVisible(i)) {
                continue;
            }

            const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);
            // マテリアルCBVはサブメッシュのマテリアルスロットに対応するインスタンスを使用
            const D3D12_GPU_VIRTUAL_ADDRESS materialCBV = MaterialCBVForSlot(subMesh.materialIndex);
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTex = (textureHandle.ptr != 0)
                ? textureHandle : textures.baseColor;

            // lodIndex の実際の範囲は DrawBatch 側で SubMeshData::GetLod がクランプする
            const InstanceBatchKey key = InstanceBatchKey::Make(
                resource_, i, lodIndex,
                baseColorTex, textures.normal, textures.metallicRoughness,
                textures.occlusion, textures.emissive,
                materialCBV, isGBufferPass,
                customForwardPSO_, customRootSignature_, customProvider_, customPipeline_);

            batch->Submit(key, mtx, materialCBV);
        }
    }

    void Model::DrawShadow(const WorldTransform& transform, ID3D12GraphicsCommandList* cmdList) {
        assert(IsInitialized());
        assert(cmdList);
        assert(renderContext_.shadowRenderer);

        ID3D12Resource* transformBuffer = GetShadowTransformBuffer();
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

        // シャドウ描画パケットを組み立てる（スキニングモデルの場合は描画前にGPUスキニング(CS)を実行）
        ShadowDrawPacket packet;
        if (HasSkinCluster()) {
            EnsureGPUSkinning(cmdList, renderContext_.shadowRenderer->GetCurrentPipelineState());
            packet.vertexBufferView = skinCluster_->outputVertexBufferView;
        } else {
            packet.vertexBufferView = resource_->GetVertexBufferView();
        }
        packet.indexBufferView = resource_->GetIndexBufferView();
        packet.indexCount = resource_->GetIndexCount();
        packet.transformCBV = transformBuffer->GetGPUVirtualAddress();

        renderContext_.shadowRenderer->BindShadowDrawPacket(cmdList, packet);
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

ModelDrawPacket Model::BuildSkinningDrawPacket(
    const SubMeshData& subMesh,
    D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE emissiveTexture) const
{
    ID3D12Resource* transformBuffer = GetGameTransformBuffer();
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
    packet.materialCBV = MaterialCBVForSlot(subMesh.materialIndex);
    packet.baseColorSRV = baseColorTexture;
    packet.normalMapSRV = normalTexture;
    packet.metallicRoughnessSRV = metallicRoughnessTexture;
    packet.occlusionSRV = occlusionTexture;
    packet.emissiveSRV = emissiveTexture;
    packet.isSkinned = true;
    return packet;
}

ID3D12Resource* Model::GetGameTransformBuffer() const
{
    const UINT frameIndex = renderContext_.dxCommon->GetSwapChain()->GetCurrentBackBufferIndex();
    assert(frameIndex < gameTransformBuffers_.size());
    return gameTransformBuffers_[frameIndex].Get();
}

ID3D12Resource* Model::GetShadowTransformBuffer() const
{
    const UINT frameIndex = renderContext_.dxCommon->GetSwapChain()->GetCurrentBackBufferIndex();
    assert(frameIndex < shadowTransformBuffers_.size());
    return shadowTransformBuffers_[frameIndex].Get();
}

// ===== クエリ =====

D3D12_GPU_VIRTUAL_ADDRESS Model::MaterialCBVForSlot(uint32_t materialIndex) const {
    assert(!materialInstances_.empty());
    const size_t index = materialIndex < materialInstances_.size() ? materialIndex : 0;
    // オーバーライド済みならそちらを、未オーバーライドなら ModelResource 共有のデフォルトを使う
    if (materialInstances_[index]) {
        return materialInstances_[index]->GetGPUVirtualAddress();
    }
    const MaterialInstance* def = resource_->GetDefaultMaterial(static_cast<uint32_t>(index));
    return def ? def->GetGPUVirtualAddress() : 0;
}

MaterialInstance* Model::GetMaterial(size_t materialIndex) {
    if (materialIndex >= materialInstances_.size()) {
        return nullptr;
    }
    if (!materialInstances_[materialIndex]) {
        // Copy-on-Write: 初回の書き込みアクセス時にのみ ModelResource の共有デフォルト値を複製する
        auto instance = std::make_unique<MaterialInstance>();
        instance->Initialize(renderContext_.dxCommon->GetDevice());
        if (const MaterialInstance* def = resource_->GetDefaultMaterial(static_cast<uint32_t>(materialIndex))) {
            instance->FromJson(def->ToJson());
        }
        materialInstances_[materialIndex] = std::move(instance);
    }
    return materialInstances_[materialIndex].get();
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
