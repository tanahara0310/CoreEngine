#include "Model.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Shadow/ShadowMapManager.h"
#include "Engine/Camera/ICamera.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Engine/Graphics/Model/Skeleton/SkeletonAnimator.h"
#include "Engine/Graphics/Model/Skeleton/SkinClusterGenerator.h"
#include "Engine/Graphics/Model/Animation/AnimationBlender.h"
#include "Engine/Utility/Logger/Logger.h"
#include "Engine/Math/MathCore.h"

#include <cassert>


namespace CoreEngine
{
    namespace {
        DirectXCommon* sDxCommon_ = nullptr;
        ResourceFactory* sResourceFactory_ = nullptr;
        ShadowMapManager* sShadowMapManager_ = nullptr;
    }

    void Model::Initialize(DirectXCommon* dxCommon, ResourceFactory* factory) {
        assert(dxCommon && factory);
        sDxCommon_ = dxCommon;
        sResourceFactory_ = factory;
    }

    void Model::SetShadowMapManager(ShadowMapManager* shadowMapManager) {
        sShadowMapManager_ = shadowMapManager;
    }

    void Model::Initialize(ModelResource* resource) {
        assert(resource && resource->IsLoaded());
        resource_ = resource;

        // マテリアルマネージャーを作成
        materialManager_ = std::make_unique<MaterialManager>();
        materialManager_->Initialize(sDxCommon_->GetDevice(), sResourceFactory_);
        materialManager_->SetEnableLighting(true);

        // WVP行列用のリソースを作成（1つのみ）
        wvpResource_ = ResourceFactory::CreateBufferResource(
            sDxCommon_->GetDevice(),
            sizeof(TransformationMatrix)
        );

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
        assert(wvpResource_);

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
        wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        mappedData->world = worldMatrix;
        mappedData->WVP = worldViewProjectionMatrix;
        mappedData->worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mappedData->lightViewProjection = lightVP;
        wvpResource_->Unmap(0, nullptr);
    }

    void Model::Draw(const WorldTransform& transform, const ICamera* camera,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle) {

        assert(IsInitialized());
        assert(camera);

        ID3D12GraphicsCommandList* cmdList = sDxCommon_->GetCommandList();
        assert(cmdList);

		// WVP行列を更新（共通処理）
		UpdateTransformationMatrix(transform, camera);
		
		// 複数サブメッシュがある場合はサブメッシュごとに描画
		const auto& subMeshes = resource_->GetSubMeshes();
		
		if (!subMeshes.empty()) {
            // サブメッシュごとに描画
            for (const auto& subMesh : subMeshes) {
                // マテリアルのテクスチャを取得
                const auto& textures = resource_->GetMaterialTextures(subMesh.materialIndex);
                
                // スキンクラスターの有無で描画方法を自動判別
                if (HasSkinCluster()) {
                    SetupSkinningDrawCommands(cmdList, textures.baseColor, textures.normal, 
                        textures.metallicRoughness, textures.occlusion);
                } else {
                    SetupNormalDrawCommands(cmdList, textures.baseColor, textures.normal, 
                        textures.metallicRoughness, textures.occlusion);
                }
                
                // このサブメッシュの範囲を描画
                cmdList->DrawIndexedInstanced(subMesh.indexCount, 1, subMesh.startIndex, 0, 0);
            }
		} else {
			// 下位互換性：サブメッシュがない場合は全体を1つのテクスチャで描画
			// デフォルトの白テクスチャを使用（空のハンドルを渡すとデフォルトテクスチャが使われる）
			D3D12_GPU_DESCRIPTOR_HANDLE emptyHandle = {};
			
			if (HasSkinCluster()) {
				SetupSkinningDrawCommands(cmdList, textureHandle, emptyHandle, emptyHandle, emptyHandle);
			} else {
				SetupNormalDrawCommands(cmdList, textureHandle, emptyHandle, emptyHandle, emptyHandle);
			}
			
			// 描画実行
			cmdList->DrawIndexedInstanced(resource_->indexCount_, 1, 0, 0, 0);
		}
	}

    void Model::DrawShadow(const WorldTransform& transform, ID3D12GraphicsCommandList* cmdList) {
        assert(IsInitialized());
        assert(cmdList);

        // ShadowMapManagerからライトVP行列を取得
        Matrix4x4 lightVP = sShadowMapManager_ ? 
            sShadowMapManager_->GetLightViewProjection() : MathCore::Matrix::Identity();

        // シャドウマップ用のWVP行列を計算（ライトVP行列を使用）
        Matrix4x4 worldMatrix = transform.GetWorldMatrix();
        Matrix4x4 lightWVP = MathCore::Matrix::Multiply(worldMatrix, lightVP);

        // GPUメモリに書き込み
        TransformationMatrix* mappedData = nullptr;
        wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        mappedData->WVP = lightWVP;
        mappedData->world = worldMatrix;
        mappedData->worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(worldMatrix));
        mappedData->lightViewProjection = lightVP;
        wvpResource_->Unmap(0, nullptr);

        // 頂点バッファを設定
        cmdList->IASetVertexBuffers(0, 1, &resource_->vertexBufferView_);

        // インデックスバッファを設定
        cmdList->IASetIndexBuffer(&resource_->indexBufferView_);

        // WVP行列を設定（ShadowMapRendererのRoot Parameter 0）
        cmdList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress());

        // スキニングモデルの場合はMatrixPaletteも設定
        if (HasSkinCluster()) {
            // スキニング用の頂点バッファも設定（slot 1）
            D3D12_VERTEX_BUFFER_VIEW skinningVBV = skinCluster_->influenceBufferView;
            cmdList->IASetVertexBuffers(1, 1, &skinningVBV);

            // MatrixPalette SRV（ShadowMapRendererのRoot Parameter 1）
            cmdList->SetGraphicsRootDescriptorTable(1, skinCluster_->paletteSrvHandle.second);
        }

        // 描画実行
        cmdList->DrawIndexedInstanced(resource_->indexCount_, 1, 0, 0, 0);
    }

    void Model::SetUVTransform(const Matrix4x4& uvTransform) {
        if (materialManager_) {
            materialManager_->SetUVTransform(uvTransform);
        }
    }

    Matrix4x4 Model::GetUVTransform() const {
        if (materialManager_) {
            return materialManager_->GetUVTransform();
        }
        return MathCore::Matrix::Identity();
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
            Logger::GetInstance().Log("Cannot switch animation: ModelResource is null",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        // アニメーションを取得
        const Animation* newAnimation = resource_->GetAnimation(animationName);
        if (!newAnimation) {
            Logger::GetInstance().Log("Animation not found: " + animationName,
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        // SkeletonAnimatorの場合のみアニメーション切り替えが可能
        auto* skeletonAnimator = dynamic_cast<SkeletonAnimator*>(animationController_.get());
        if (!skeletonAnimator) {
            Logger::GetInstance().Log("Animation switching is only supported for SkeletonAnimator",
                LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }

        // 現在のスケルトン状態を保持したまま、新しいアニメーションコントローラーを作成
        Skeleton currentSkeleton = skeletonAnimator->GetSkeleton();
        animationController_ = std::make_unique<SkeletonAnimator>(currentSkeleton, *newAnimation, loop);

        Logger::GetInstance().Log("Switched to animation: " + animationName,
            LogLevel::INFO, LogCategory::Graphics);

        return true;
    }

    bool Model::SwitchAnimationWithBlend(const std::string& animationName, float blendDuration, bool loop) {
        // リソースがない場合は失敗
        if (!resource_) {
            Logger::GetInstance().Log("Cannot switch animation: ModelResource is null",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        // アニメーションを取得
        const Animation* newAnimation = resource_->GetAnimation(animationName);
        if (!newAnimation) {
            Logger::GetInstance().Log("Animation not found: " + animationName,
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        // AnimationBlenderとして動作している場合
        auto* blender = dynamic_cast<AnimationBlender*>(animationController_.get());
        if (blender) {
            // 現在のスケルトンを取得して新しいアニメーターを作成
            Skeleton currentSkeleton = blender->GetSkeleton();
            auto newAnimator = std::make_unique<SkeletonAnimator>(currentSkeleton, *newAnimation, loop);
            blender->StartBlend(std::move(newAnimator), blendDuration);

            Logger::GetInstance().Log("Started blend to animation: " + animationName,
                LogLevel::INFO, LogCategory::Graphics);
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

            Logger::GetInstance().Log("Started blend to animation: " + animationName,
                LogLevel::INFO, LogCategory::Graphics);
            return true;
        }

        Logger::GetInstance().Log("Animation blending is only supported for SkeletonAnimator",
            LogLevel::WARNING, LogCategory::Graphics);
        return false;
    }

    // ===== PBRテクスチャ対応のSetup描画コマンド =====
    
    void Model::SetupNormalDrawCommands(ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture) {

        // 頂点バッファを設定
        cmdList->IASetVertexBuffers(0, 1, &resource_->vertexBufferView_);

        // インデックスバッファを設定
        cmdList->IASetIndexBuffer(&resource_->indexBufferView_);

        // マテリアルを設定（Root Parameter 0）
        cmdList->SetGraphicsRootConstantBufferView(
            ModelRendererRootParam::kMaterial,
            materialManager_->GetGPUVirtualAddress()
        );

        // WVP行列を設定（Root Parameter 1）
        cmdList->SetGraphicsRootConstantBufferView(
            ModelRendererRootParam::kWVP,
            wvpResource_->GetGPUVirtualAddress()
        );

        // BaseColorテクスチャを設定（Root Parameter 2）
        cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kTexture, baseColorTexture);

        // ===== PBRテクスチャマップを設定 =====
        
        // ノーマルマップを設定（Root Parameter 12）
        if (normalTexture.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kNormalMap, normalTexture);
        }

        // MetallicRoughnessマップを設定（Root Parameter 13, 14）
        // glTFのMetallicRoughnessテクスチャはRチャンネル=Occlusion, Gチャンネル=Roughness, Bチャンネル=Metallic
        if (metallicRoughnessTexture.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kMetallicMap, metallicRoughnessTexture);
            cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kRoughnessMap, metallicRoughnessTexture);
        }

        // AOマップを設定（Root Parameter 15）
        if (occlusionTexture.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(ModelRendererRootParam::kAOMap, occlusionTexture);
        }
    }

    void Model::SetupSkinningDrawCommands(ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE baseColorTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE normalTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE metallicRoughnessTexture,
        D3D12_GPU_DESCRIPTOR_HANDLE occlusionTexture) {

        assert(skinCluster_.has_value());

        // 頂点バッファを2つ設定（通常の頂点データとInfluenceデータ）
        D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
            resource_->vertexBufferView_,      // Slot 0: VertexData
            skinCluster_->influenceBufferView  // Slot 1: VertexInfluence
        };
        cmdList->IASetVertexBuffers(0, 2, vbvs);

        // インデックスバッファを設定
        cmdList->IASetIndexBuffer(&resource_->indexBufferView_);

        // WVP行列を設定（Root Parameter 0）
        cmdList->SetGraphicsRootConstantBufferView(
            SkinnedModelRendererRootParam::kWVP,
            wvpResource_->GetGPUVirtualAddress()
        );

        // MatrixPaletteを設定（Root Parameter 1）
        cmdList->SetGraphicsRootDescriptorTable(
            SkinnedModelRendererRootParam::kMatrixPalette,
            skinCluster_->paletteSrvHandle.second
        );

        // マテリアルを設定（Root Parameter 2）
        cmdList->SetGraphicsRootConstantBufferView(
            SkinnedModelRendererRootParam::kMaterial,
            materialManager_->GetGPUVirtualAddress()
        );

        // BaseColorテクスチャを設定（Root Parameter 3）
        cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kTexture, baseColorTexture);

        // ===== PBRテクスチャマップを設定 =====
        
        // ノーマルマップを設定
        if (normalTexture.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kNormalMap, normalTexture);
        }

        // MetallicRoughnessマップを設定
        if (metallicRoughnessTexture.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kMetallicMap, metallicRoughnessTexture);
            cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kRoughnessMap, metallicRoughnessTexture);
        }

        // AOマップを設定
        if (occlusionTexture.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(SkinnedModelRendererRootParam::kAOMap, occlusionTexture);
        }
    }
}
