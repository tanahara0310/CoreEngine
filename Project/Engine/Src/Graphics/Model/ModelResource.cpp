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
    
    // 頂点数を設定
    vertexCount_ = static_cast<UINT>(modelData_.vertices.size());
    
    // インデックス数を設定
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
    
    // ===== マテリアルのPBRテクスチャを読み込み =====
    materialTextureHandles_.resize(modelData_.materials.size());
    
    // デフォルトの白テクスチャを取得（テクスチャが存在しない場合に使用）
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
        
        // MetallicRoughnessテクスチャ
        if (!material.metallicRoughnessTexture.empty()) {
            handles.metallicRoughness = textureManager_->Load(material.metallicRoughnessTexture).gpuHandle;
            handles.hasMetallicRoughness = true;
        } else {
            handles.metallicRoughness = defaultWhiteTexture;
            handles.hasMetallicRoughness = false;
        }

        // Normalマップ
        if (!material.normalTexture.empty()) {
            handles.normal = textureManager_->Load(material.normalTexture).gpuHandle;
            handles.hasNormal = true;
        } else {
            handles.normal = defaultWhiteTexture;
            handles.hasNormal = false;
        }

        // Occlusionマップ
        if (!material.occlusionTexture.empty()) {
            handles.occlusion = textureManager_->Load(material.occlusionTexture).gpuHandle;
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
