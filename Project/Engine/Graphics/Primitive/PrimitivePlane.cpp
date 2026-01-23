#include "PrimitivePlane.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include <cassert>


namespace CoreEngine
{
void PrimitivePlane::Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory,
								 float width, float height) {
	assert(dxCommon && resourceFactory);
	dxCommon_ = dxCommon;
	resourceFactory_ = resourceFactory;
	currentWidth_ = width;
	currentHeight_ = height;

	CreateBuffers();
	CreateVertices(width, height);
}

void PrimitivePlane::CreateBuffers() {
	auto* device = dxCommon_->GetDevice();

	// 頂点バッファの作成
	UINT vertexBufferSize = sizeof(VertexData) * vertexCount_;
	vertexBuffer_ = resourceFactory_->CreateBufferResource(device, vertexBufferSize);

	// 頂点バッファビューの設定
	vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = vertexBufferSize;
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// 頂点データをマップ
	vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData_));

	// インデックスバッファの作成
	UINT indexBufferSize = sizeof(uint32_t) * indexCount_;
	indexBuffer_ = resourceFactory_->CreateBufferResource(device, indexBufferSize);

	// インデックスバッファビューの設定
	indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = indexBufferSize;
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	// インデックスデータを書き込み
	uint32_t* mappedIndexData = nullptr;
	indexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedIndexData));
	
	// 2つの三角形でPlaneを構成（時計回り）
	// 0---1
	// |  /|
	// | / |
	// |/  |
	// 2---3
	mappedIndexData[0] = 0;
	mappedIndexData[1] = 1;
	mappedIndexData[2] = 2;
	mappedIndexData[3] = 1;
	mappedIndexData[4] = 3;
	mappedIndexData[5] = 2;

	indexBuffer_->Unmap(0, nullptr);
}

void PrimitivePlane::CreateVertices(float width, float height) {
	if (!mappedVertexData_) return;

	float halfWidth = width * 0.5f;
	float halfHeight = height * 0.5f;

	// XZ平面上に水平なPlaneを作成（Y軸上向きの法線）
	// 左上
	mappedVertexData_[0].position = { -halfWidth, 0.0f, halfHeight, 1.0f };
	mappedVertexData_[0].texcoord = { 0.0f, 0.0f };
	mappedVertexData_[0].normal = { 0.0f, 1.0f, 0.0f };

	// 右上
	mappedVertexData_[1].position = { halfWidth, 0.0f, halfHeight, 1.0f };
	mappedVertexData_[1].texcoord = { 1.0f, 0.0f };
	mappedVertexData_[1].normal = { 0.0f, 1.0f, 0.0f };

	// 左下
	mappedVertexData_[2].position = { -halfWidth, 0.0f, -halfHeight, 1.0f };
	mappedVertexData_[2].texcoord = { 0.0f, 1.0f };
	mappedVertexData_[2].normal = { 0.0f, 1.0f, 0.0f };

	// 右下
	mappedVertexData_[3].position = { halfWidth, 0.0f, -halfHeight, 1.0f };
	mappedVertexData_[3].texcoord = { 1.0f, 1.0f };
	mappedVertexData_[3].normal = { 0.0f, 1.0f, 0.0f };
}

void PrimitivePlane::Resize(float width, float height) {
	currentWidth_ = width;
	currentHeight_ = height;
	CreateVertices(width, height);
}
}
