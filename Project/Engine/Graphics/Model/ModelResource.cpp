#include "ModelResource.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Model/ModelLoader.h"
#include "Engine/Graphics/Model/Skeleton/SkeletonLoader.h"
#include "Engine/Graphics/Structs/VertexData.h"

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
	modelData_ = modelData;
	
	// RootNodeを保存
	rootNode_ = modelData.rootNode;
	
	// Skeletonを作成
	skeleton_ = SkeletonLoader::CreateSkeleton(modelData.rootNode);
	
	// マテリアルデータを保存
	materialData_ = modelData.material;
	
	// 頂点数を設定
	vertexCount_ = static_cast<UINT>(modelData.vertices.size());
	
	// インデックス数を設定
	indexCount_ = static_cast<UINT>(modelData.indices.size());
	
	// 頂点バッファの作成
	vertexBuffer_ = ResourceFactory::CreateBufferResource(
		dxCommon_->GetDevice(),
		sizeof(VertexData) * modelData.vertices.size());

	// 頂点バッファビューの設定
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// 頂点データをGPUメモリにコピー
	void* mapped = nullptr;
	vertexBuffer_->Map(0, nullptr, &mapped);
	memcpy(mapped, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
	vertexBuffer_->Unmap(0, nullptr);
	
	// インデックスバッファの作成
	indexBuffer_ = ResourceFactory::CreateBufferResource(
		dxCommon_->GetDevice(),
		sizeof(uint32_t) * modelData.indices.size());
	
	// インデックスバッファビューの設定
	indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * modelData.indices.size());
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
	
	// インデックスデータをGPUメモリにコピー
	void* mappedIndex = nullptr;
	indexBuffer_->Map(0, nullptr, &mappedIndex);
	memcpy(mappedIndex, modelData.indices.data(), sizeof(uint32_t) * modelData.indices.size());
	indexBuffer_->Unmap(0, nullptr);
	
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
}
