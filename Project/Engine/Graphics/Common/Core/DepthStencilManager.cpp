#include "DepthStencilManager.h"
#include "DescriptorManager.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"

#include <cassert>
#include <format>

using namespace Microsoft::WRL;

namespace CoreEngine
{
	namespace {
		Logger& logger = Logger::GetInstance();
	}

	void DepthStencilManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager,
		std::int32_t width, std::int32_t height)
	{
		assert(device != nullptr && "Device must not be null");
		assert(descriptorManager != nullptr && "DescriptorManager must not be null");

		device_ = device;
		descriptorManager_ = descriptorManager;
		width_ = width;
		height_ = height;

		CreateDepthStencilResource();

		if (!isInitialized_) {
			// 初回のみDSVを作成
			CreateDepthStencilView();
			isInitialized_ = true;
		} else {
			// 2回目以降は既存のハンドルでDSVを更新
			UpdateDepthStencilView();
		}
	}

	void DepthStencilManager::ResizeResource(std::int32_t width, std::int32_t height)
	{
		assert(isInitialized_ && "DepthStencilManager must be initialized first");

		width_ = width;
		height_ = height;

		// リソースを再作成
		CreateDepthStencilResource();

		// 既存のハンドルでDSVを更新
		UpdateDepthStencilView();

#ifdef _DEBUG
		logger.Log(
			std::format("深度ステンシルリソースをリサイズしました ({}x{}) - DSVは再利用\n", width, height),
			LogLevel::INFO, LogCategory::Graphics);
#endif
	}

	void DepthStencilManager::CreateDepthStencilResource()
	{
		// リソース設定
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = width_;
		resourceDesc.Height = height_;
		resourceDesc.MipLevels = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// ヒープの設定
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		// クリア値の設定
		D3D12_CLEAR_VALUE clearValue{};
		clearValue.DepthStencil.Depth = 1.0f;
		clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

		// リソースを作成
		HRESULT hr = device_->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&clearValue,
			IID_PPV_ARGS(&depthStencilResource_));

		if (FAILED(hr)) {
			logger.Log(
				std::format("エラー: 深度ステンシルリソースの作成に失敗しました! 幅={}, 高さ={}\n",
					width_, height_),
				LogLevel::Error, LogCategory::Graphics);
			throw std::runtime_error("Failed to create DepthStencilResource");
		}

#ifdef _DEBUG
		logger.Log(
			std::format("深度ステンシルリソースを作成しました ({}x{})\n", width_, height_),
			LogLevel::INFO, LogCategory::Graphics);
#endif
	}

	void DepthStencilManager::CreateDepthStencilView()
	{
		// DSVの設定
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		// DescriptorManagerを使ってDSVを作成（初回のみ）
		descriptorManager_->CreateDSV(
			depthStencilResource_.Get(),
			dsvDesc,
			dsvHandle_,
			"MainDepthStencil"
		);
	}

	void DepthStencilManager::UpdateDepthStencilView()
	{
		// DSVの設定
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		// 既存のハンドルに直接DSVを更新
		device_->CreateDepthStencilView(
			depthStencilResource_.Get(),
			&dsvDesc,
			dsvHandle_);

#ifdef _DEBUG
		logger.Log(
			std::format("DSVを更新しました (既存ハンドルを再利用)\n"),
			LogLevel::INFO, LogCategory::Graphics);
#endif
	}
}
