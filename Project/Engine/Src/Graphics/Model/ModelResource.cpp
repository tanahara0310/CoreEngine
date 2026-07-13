#include "pch.h"
#include "ModelResource.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Model/Skeleton/SkeletonLoader.h"
#include "Graphics/Model/VertexData.h"

#include <cassert>


namespace CoreEngine
{
    void ModelResource::Initialize(DirectXCommon* dxCommon, ResourceFactory* factory, TextureManager* textureMg)
    {
        dxCommon_ = dxCommon;
        resourceFactory_ = factory;
        textureManager_ = textureMg;
    }

    void ModelResource::CreateGeometryBuffers()
    {
        // 頂点数・インデックス数を設定
        vertexCount_ = static_cast<UINT>(modelData_.vertices.size());
        indexCount_ = static_cast<UINT>(modelData_.indices.size());

        // 頂点バッファの作成
        vertexBuffer_ = ResourceFactory::CreateBufferResource(
            dxCommon_->GetDevice(),
            sizeof(VertexData) * modelData_.vertices.size());

        // 頂点バッファビューの設定
        vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData_.vertices.size());
        vertexBufferView_.StrideInBytes = sizeof(VertexData);

        // 頂点データをGPUメモリにコピー
        void* mapped = nullptr;
        vertexBuffer_->Map(0, nullptr, &mapped);
        memcpy(mapped, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
        vertexBuffer_->Unmap(0, nullptr);

        // インデックスバッファの作成
        indexBuffer_ = ResourceFactory::CreateBufferResource(
            dxCommon_->GetDevice(),
            sizeof(uint32_t) * modelData_.indices.size());

        // インデックスバッファビューの設定
        indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * modelData_.indices.size());
        indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

        // インデックスデータをGPUメモリにコピー
        void* mappedIndex = nullptr;
        indexBuffer_->Map(0, nullptr, &mappedIndex);
        memcpy(mappedIndex, modelData_.indices.data(), sizeof(uint32_t) * modelData_.indices.size());
        indexBuffer_->Unmap(0, nullptr);

        // ===== ローカル空間AABBを頂点データから算出 =====
        localBoundingBox_ = BoundingBox(); // 無効値で初期化
        for (const auto& vertex : modelData_.vertices) {
            if (vertex.position.x < localBoundingBox_.min.x) localBoundingBox_.min.x = vertex.position.x;
            if (vertex.position.y < localBoundingBox_.min.y) localBoundingBox_.min.y = vertex.position.y;
            if (vertex.position.z < localBoundingBox_.min.z) localBoundingBox_.min.z = vertex.position.z;
            if (vertex.position.x > localBoundingBox_.max.x) localBoundingBox_.max.x = vertex.position.x;
            if (vertex.position.y > localBoundingBox_.max.y) localBoundingBox_.max.y = vertex.position.y;
            if (vertex.position.z > localBoundingBox_.max.z) localBoundingBox_.max.z = vertex.position.z;
        }
    }

    void ModelResource::LoadFromFile(const std::string& directoryPath, const std::string& filename)
    {
        assert(dxCommon_ && resourceFactory_ && textureManager_);

        // ModelLoaderを使用してモデルデータを読み込む
        ModelData modelData = ModelLoader::LoadModelFile(directoryPath, filename);

        // ModelDataを保存（スキンクラスター生成に必要）
        // moveセマンティクスを使って大きなデータを効率的に移動
        modelData_ = std::move(modelData);

        // RootNodeを保存
        rootNode_ = modelData_.rootNode;

        // Skeletonを作成
        skeleton_ = SkeletonLoader::CreateSkeleton(modelData_.rootNode);

        CreateGeometryBuffers();

        // ===== マテリアルのPBRテクスチャを並列読み込み =====
        // 色空間: ベースカラー/エミッシブは sRGB、法線/MR/AO はデータテクスチャなので Linear。
        materialTextureHandles_.resize(modelData_.materials.size());

        // 全マテリアルで必要なテクスチャリクエストを収集する。
        std::vector<TextureManager::LoadRequest> textureRequests;
        textureRequests.push_back({ "white1x1.png", TextureColorSpace::SRGB });

        for (size_t i = 0; i < modelData_.materials.size(); ++i) {
            const MaterialAsset& material = modelData_.materials[i];
            if (!material.baseColorTexture.empty())
                textureRequests.push_back({ material.baseColorTexture, TextureColorSpace::SRGB });
            if (!material.metallicRoughnessTexture.empty())
                textureRequests.push_back({ material.metallicRoughnessTexture, TextureColorSpace::Linear });
            if (!material.normalTexture.empty())
                textureRequests.push_back({ material.normalTexture, TextureColorSpace::Linear });
            if (!material.occlusionTexture.empty())
                textureRequests.push_back({ material.occlusionTexture, TextureColorSpace::Linear });
            if (!material.emissiveTexture.empty())
                textureRequests.push_back({ material.emissiveTexture, TextureColorSpace::SRGB });
        }

        // 収集したリクエストを一括で並列ロードに投入し、完了を待つ。
        textureManager_->Load(textureRequests);

        // 全テクスチャがキャッシュ済みの状態で割り当てる（キャッシュヒットのみ）。
        // 白1x1は値が(1,1,1,1)のため sRGB/Linear どちらのフォールバックとしても正しい。
        D3D12_GPU_DESCRIPTOR_HANDLE defaultWhiteTexture = textureManager_->Load("white1x1.png").gpuHandle;

        for (size_t i = 0; i < modelData_.materials.size(); ++i) {
            const MaterialAsset& material = modelData_.materials[i];
            PBRTextureHandles& handles = materialTextureHandles_[i];

            // BaseColor（Albedo）テクスチャ
            if (!material.baseColorTexture.empty()) {
                handles.baseColor = textureManager_->Load(material.baseColorTexture).gpuHandle;
            } else {
                handles.baseColor = defaultWhiteTexture;
            }

            // MetallicRoughnessテクスチャ (G=Roughness, B=Metallic)
            if (!material.metallicRoughnessTexture.empty()) {
                handles.metallicRoughness = textureManager_->Load(material.metallicRoughnessTexture, TextureColorSpace::Linear).gpuHandle;
                handles.hasMetallicRoughness = true;
            } else {
                handles.metallicRoughness = defaultWhiteTexture;
                handles.hasMetallicRoughness = false;
            }

            // Normalマップ
            if (!material.normalTexture.empty()) {
                handles.normal = textureManager_->Load(material.normalTexture, TextureColorSpace::Linear).gpuHandle;
                handles.hasNormal = true;
            } else {
                handles.normal = defaultWhiteTexture;
                handles.hasNormal = false;
            }

            // Occlusionマップ
            if (!material.occlusionTexture.empty()) {
                handles.occlusion = textureManager_->Load(material.occlusionTexture, TextureColorSpace::Linear).gpuHandle;
                handles.hasOcclusion = true;
            } else {
                handles.occlusion = defaultWhiteTexture;
                handles.hasOcclusion = false;
            }

            // Emissiveマップ
            if (!material.emissiveTexture.empty()) {
                handles.emissive = textureManager_->Load(material.emissiveTexture).gpuHandle;
                handles.hasEmissive = true;
            } else {
                handles.emissive = defaultWhiteTexture;
                handles.hasEmissive = false;
            }
        }

        // ファイルパスを保存（デバッグ用）
        filePath_ = directoryPath + "/" + filename;
        isLoaded_ = true;
    }

    void ModelResource::LoadFromModelData(ModelData&& data, const std::string& name)
    {
        assert(dxCommon_ && resourceFactory_ && textureManager_);

        modelData_ = std::move(data);

        // RootNodeを保存
        rootNode_ = modelData_.rootNode;

        CreateGeometryBuffers();

        // マテリアルテクスチャハンドルの設定（デフォルト白テクスチャ）
        D3D12_GPU_DESCRIPTOR_HANDLE defaultWhiteTexture = textureManager_->Load("white1x1.png").gpuHandle;
        materialTextureHandles_.resize(std::max<size_t>(modelData_.materials.size(), 1));
        for (auto& handles : materialTextureHandles_) {
            handles.baseColor = defaultWhiteTexture;
            handles.metallicRoughness = defaultWhiteTexture;
            handles.normal = defaultWhiteTexture;
            handles.occlusion = defaultWhiteTexture;
            handles.emissive = defaultWhiteTexture;
        }

        filePath_ = name.empty() ? "<procedural>" : name;
        isLoaded_ = true;
    }

    const Animation* ModelResource::GetAnimation(const std::string& name) const {
        if (animations_.empty()) {
            return nullptr;
        }

        // 名前が空文字列の場合は最初のアニメーションを返す
        if (name.empty()) {
            return &animations_.begin()->second;
        }

        // 指定された名前のアニメーションを検索
        auto it = animations_.find(name);
        if (it != animations_.end()) {
            return &it->second;
        }

        return nullptr;
    }

    void ModelResource::AddAnimation(const std::string& name, const Animation& animation) {
        animations_[name] = animation;
    }

    const ModelResource::PBRTextureHandles& ModelResource::GetMaterialTextures(uint32_t materialIndex) const {
        assert(materialIndex < materialTextureHandles_.size());
        return materialTextureHandles_[materialIndex];
    }
}
