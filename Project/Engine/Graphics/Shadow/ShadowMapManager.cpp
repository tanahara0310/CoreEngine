#include "ShadowMapManager.h"
#include "Engine/Graphics/Common/Core/DescriptorManager.h"
#include "Engine/Utility/Logger/Logger.h"

#include <cassert>
#include <format>

using namespace Microsoft::WRL;

namespace CoreEngine
{
	namespace {
		Logger& logger = Logger::GetInstance();
	}

	void ShadowMapManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager)
	{
		assert(device != nullptr && "Device must not be null");
		assert(descriptorManager != nullptr && "DescriptorManager must not be null");

		device_ = device;
		descriptorManager_ = descriptorManager;

		// シャドウマップリソースを作成
		CreateShadowMapResource();

		// DSVを作成
		CreateDepthStencilView();

		// SRVを作成
		CreateShaderResourceView();

#ifdef _DEBUG
		logger.Log(
			std::format("ShadowMapManagerを初期化しました ({}x{})\n", shadowMapSize_, shadowMapSize_),
			LogLevel::INFO, LogCategory::Graphics);
#endif
	}

	void ShadowMapManager::ClearDepth(ID3D12GraphicsCommandList* cmdList)
	{
		assert(cmdList != nullptr && "CommandList must not be null");

		// 深度を1.0fでクリア（最大深度）
		cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	}

void ShadowMapManager::TransitionToDepthWrite(ID3D12GraphicsCommandList* cmdList)
{
	assert(cmdList != nullptr && "CommandList must not be null");
	assert(shadowMapResource_.Get() != nullptr && "ShadowMap resource must not be null");

	if (currentState_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = shadowMapResource_.Get();
		barrier.Transition.StateBefore = currentState_;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		cmdList->ResourceBarrier(1, &barrier);
		currentState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}
}

void ShadowMapManager::TransitionToShaderResource(ID3D12GraphicsCommandList* cmdList)
{
	assert(cmdList != nullptr && "CommandList must not be null");
	assert(shadowMapResource_.Get() != nullptr && "ShadowMap resource must not be null");

	if (currentState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = shadowMapResource_.Get();
		barrier.Transition.StateBefore = currentState_;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		cmdList->ResourceBarrier(1, &barrier);
		currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
}

void ShadowMapManager::CreateShadowMapResource()
	{
		// リソース設定
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = shadowMapSize_;
		resourceDesc.Height = shadowMapSize_;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = shadowMapFormat_;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// ヒープの設定
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		// クリア値の設定
		D3D12_CLEAR_VALUE clearValue{};
		clearValue.Format = shadowMapFormat_;
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.DepthStencil.Stencil = 0;

		// リソースを作成（初期ステートはSHADER_RESOURCE）
		HRESULT hr = device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&clearValue,
			IID_PPV_ARGS(&shadowMapResource_));

		if (FAILED(hr)) {
			logger.Log(
				std::format("エラー: シャドウマップリソースの作成に失敗しました! サイズ={}\n", shadowMapSize_),
				LogLevel::Error, LogCategory::Graphics);
			throw std::runtime_error("Failed to create ShadowMap Resource");
		}

		// デバッグ名を設定
		shadowMapResource_->SetName(L"ShadowMapResource");
	}

	void ShadowMapManager::CreateDepthStencilView()
	{
		// DSVの設定
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = shadowMapFormat_;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		dsvDesc.Texture2D.MipSlice = 0;

		// DescriptorManagerを使ってDSVを作成
		descriptorManager_->CreateDSV(
			shadowMapResource_.Get(),
			dsvDesc,
			dsvHandle_,
			"ShadowMapDSV"
		);
	}

	void ShadowMapManager::CreateShaderResourceView()
	{
		// SRVの設定
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // D32_FLOAT -> R32_FLOAT (深度を読み取り用)
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.PlaneSlice = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		// DescriptorManagerを使ってSRVを作成
		descriptorManager_->CreateSRV(
			shadowMapResource_.Get(),
			srvDesc,
			srvCpuHandle_,
			srvGpuHandle_,
			"ShadowMapSRV"
		);
	}
}
