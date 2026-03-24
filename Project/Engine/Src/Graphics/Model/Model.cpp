#include "Model.h"
#include "ModelRenderContext.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Camera/ICamera.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Shadow/ShadowMapRenderer.h"
#include "Graphics/Model/Skeleton/SkinClusterGenerator.h"
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
        resource_ = resource;
        renderContext_ = ctx;

        // MaterialInstanceを作成
        materialInstance_ = std::make_unique<MaterialInstance>();
        materialInstance_->Initialize(renderContext_.dxCommon->GetDevice());

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
                // PBR は常に有効。テクスチャマップフラグは上で個別に設定済み。
            }
        }

        for (auto& wvpResource : wvpResources_) {
            wvpResource = ResourceFactory::CreateBufferResource(
                renderContext_.dxCommon->GetDevice(),
                sizeof(TransformationMatrix)
            );
        }

        // Skeletonをコピー
        if (resource_->GetSkeleton()) {
            skeleton_ = *resource_->GetSkeleton();

            const ModelData& modelData = resource_->GetModelData();
            if (!modelData.skinClusterData.empty()) {
                skinCluster_ = SkinClusterGenerator::CreateSkinCluster(
                    renderContext_.dxCommon->GetDevice(),
                    *skeleton_,
                    modelData,
                    renderContext_.dxCommon->GetDescriptorManager()
                );
            }
        }
    }

    void Model::Initialize(ModelResource* resource, std::unique_ptr<IAnimationController> controller, const ModelRenderContext& ctx) {
        // 基本の初期化を実行
        Initialize(resource, ctx);

        // アニメーションコントローラーを設定
        animationController_ = std::move(controller);
    }

    void Model::UpdateSkinCluster() {
        // SkinClusterとSkeletonが両方存在する場合のみ更新
        if (skinCluster_ && skeleton_) {
            SkinClusterGenerator::Update(*skinCluster_, *skeleton_);
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
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle, TransformBufferSlot slot) {

        assert(IsInitialized());
        assert(camera);

        // 呼び出し側がスロットを明示しない場合（デフォルト Game）は
        // BaseScene が SetCurrentRenderSlot() で設定したグローバルスロットを使用する。
        // これにより SceneView(slot=Scene) と GameView/GBuffer(slot=Game) が
        // 別々の WVP バッファに書き込まれ、相互上書きを防ぐ。
        if (slot == TransformBufferSlot::Game) {
            slot = s_currentRenderSlot_;
        }

        ID3D12GraphicsCommandList* cmdList = renderContext_.dxCommon->GetCommandList();
        assert(cmdList);

        // WVP 行列を更新（slot で使用バッファを指定）
        UpdateTransformationMatrix(transform, camera, slot);

        const auto& subMeshes = resource_->GetSubMeshes();
        assert(!subMeshes.empty() && "Model must have at least one submesh");

        const bool isSkinned = HasSkinCluster();
        BaseModelRenderer* renderer = isSkinned
            ? renderContext_.skinnedRenderer
            : renderContext_.modelRenderer;
        assert(renderer);

        for (const auto& subMesh : subMeshes) {
            const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);

            // textureHandle が指定されている場合は BaseColor をオーバーライド
            D3D12_GPU_DESCRIPTOR_HANDLE baseColorTex = (textureHandle.ptr != 0)
                ? textureHandle : textures.baseColor;

            // モデル種別に応じたパケットを組み立て、レンダラーに渡す
            const ModelDrawPacket packet = isSkinned
                ? BuildSkinningDrawPacket(subMesh, baseColorTex, textures.normal,
                    textures.metallicRoughness, textures.occlusion, slot)
                : BuildNormalDrawPacket(subMesh, baseColorTex, textures.normal,
                    textures.metallicRoughness, textures.occlusion, slot);

            renderer->BindModelDrawPacket(cmdList, packet);
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

    // 頂点バッファを設定
    cmdList->IASetVertexBuffers(0, 1, &resource_->GetVertexBufferView());

    // インデックスバッファを設定
    cmdList->IASetIndexBuffer(&resource_->GetIndexBufferView());

    // WVP行列を設定（シェーダーリフレクションからインデックスを取得）
    int lightTransformIdx = renderContext_.shadowRenderer ? renderContext_.shadowRenderer->GetRootParamIndex("gLightTransform") : 0;
    if (lightTransformIdx >= 0) {
        cmdList->SetGraphicsRootConstantBufferView(lightTransformIdx, transformBuffer->GetGPUVirtualAddress());
    }

    // スキニングモデルの場合はMatrixPaletteも設定
    if (HasSkinCluster()) {
        // スキニング用の頂点バッファも設定（slot 1）
        D3D12_VERTEX_BUFFER_VIEW skinningVBV = skinCluster_->influenceBufferView;
        cmdList->IASetVertexBuffers(1, 1, &skinningVBV);

        // MatrixPalette SRV（シェーダーリフレクションからインデックスを取得）
        int matrixPaletteIdx = renderContext_.shadowRenderer ? renderContext_.shadowRenderer->GetRootParamIndex("gMatrixPalette") : 1;
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
    cmdList->DrawIndexedInstanced(resource_->GetIndexCount(), 1, 0, 0, 0);
}

void Model::UpdateAnimation(float deltaTime) {
    if (!animationController_) return;

    // アニメーション時刻を進める
    animationController_->Update(deltaTime);

    // スケルトンが存在する場合はインスタンス変数に同期し、SkinCluster も更新する
    if (const Skeleton* skel = animationController_->GetSkeleton()) {
        skeleton_ = *skel;
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

void Model::SetAnimationControllerFactory(std::unique_ptr<IAnimationControllerFactory> factory)
{
    animationFactory_ = std::move(factory);
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

    // アニメーションコントローラーがない場合は失敗
    if (!animationController_) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Cannot switch animation: no animation controller");
        return false;
    }

    // ファクトリーが未設定の場合は失敗
    if (!animationFactory_) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "AnimationControllerFactory is not set. Use ModelManager::CreateSkeletonModel()");
        return false;
    }

    // スケルトンを持つコントローラーのみアニメーション切り替えが可能
    const Skeleton* skel = animationController_->GetSkeleton();
    if (!skel) {
        Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", "Animation switching is only supported for SkeletonAnimator");
        return false;
    }

    // ファクトリー経由で新しいコントローラーを生成
    animationController_ = animationFactory_->CreateSkeletonAnimator(*skel, *newAnimation, loop);

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

    // アニメーションコントローラーがない場合は失敗
    if (!animationController_) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Cannot switch animation: no animation controller");
        return false;
    }

    // ファクトリーが未設定の場合は失敗
    if (!animationFactory_) {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "AnimationControllerFactory is not set. Use ModelManager::CreateSkeletonModel()");
        return false;
    }

    // スケルトンを持つコントローラーのみブレンド可能
    const Skeleton* skel = animationController_->GetSkeleton();
    if (!skel) {
        Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", "Animation blending is only supported for SkeletonAnimator");
        return false;
    }
    Skeleton currentSkeleton = *skel;

    // ファクトリー経由でブレンド先コントローラーを生成
    auto newAnimator = animationFactory_->CreateSkeletonAnimator(currentSkeleton, *newAnimation, loop);

    if (animationController_->IsBlending()) {
        // 既に AnimationBlender として動作中 → ターゲットだけ切り替える（dynamic_cast 不要）
        animationController_->AddBlendTarget(std::move(newAnimator), blendDuration);
    } else {
        // SkeletonAnimator からブレンダーへ置き換え
        animationController_ = animationFactory_->CreateBlenderWithTarget(
            std::move(animationController_), std::move(newAnimator), blendDuration);
    }

    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Started blend to animation: " + animationName);
    return true;
}

// ===== ModelDrawPacket 組み立て =====

ModelDrawPacket Model::BuildNormalDrawPacket(
    const SubMeshData& subMesh,
    D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
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
    packet.transformCBV = transformBuffer->GetGPUVirtualAddress();
    packet.materialCBV = materialInstance_->GetGPUVirtualAddress();
    packet.baseColorSRV = baseColorTexture;
    packet.normalMapSRV = normalTexture;
    packet.metallicRoughnessSRV = metallicRoughnessTexture;
    packet.occlusionSRV = occlusionTexture;
    packet.isSkinned = false;
    return packet;
}

ModelDrawPacket Model::BuildSkinningDrawPacket(
    const SubMeshData& subMesh,
    D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
    D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture,
    TransformBufferSlot slot) const
{
    ID3D12Resource* transformBuffer = GetTransformBuffer(slot);
    assert(transformBuffer);
    assert(skinCluster_.has_value());
    assert(renderContext_.skinnedRenderer);

    ModelDrawPacket packet;
    packet.vertexBufferViews[0] = resource_->GetVertexBufferView();
    packet.vertexBufferViews[1] = skinCluster_->influenceBufferView;
    packet.vertexBufferViewCount = 2;
    packet.indexBufferView = resource_->GetIndexBufferView();
    packet.indexCount = subMesh.indexCount;
    packet.startIndex = subMesh.startIndex;
    packet.transformCBV = transformBuffer->GetGPUVirtualAddress();
    packet.materialCBV = materialInstance_->GetGPUVirtualAddress();
    packet.baseColorSRV = baseColorTexture;
    packet.normalMapSRV = normalTexture;
    packet.metallicRoughnessSRV = metallicRoughnessTexture;
    packet.occlusionSRV = occlusionTexture;
    packet.isSkinned = true;
    packet.matrixPaletteSRV = skinCluster_->paletteSrvHandle.second;
    return packet;
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


