#include "DescriptorManager.h"
#include "Engine/Utility/Logger/Logger.h"

#include <cassert>
#include <format>


namespace CoreEngine
{
namespace {
	Logger& logger = Logger::GetInstance();
}

void DescriptorManager::Initialize(ID3D12Device* device)
{
	assert(device != nullptr && "Device must not be null");
	device_ = device;             
	CreateDescriptorHeaps();

	logger.Log(
		std::format("DescriptorManager初期化完了: SRV最大数={}, RTV最大数={}, DSV最大数={}\n",
			kMaxSRVDescriptors, kMaxRTVDescriptors, kMaxDSVDescriptors),
		LogLevel::INFO, LogCategory::Graphics);

#ifdef _DEBUG
	// 予約済みインデックスの情報を出力
	logger.Log(
		std::format("予約済みディスクリプタ: RTV[0-1]=スワップチェーン, SRV[0]=ImGui用, DSV[なし]\n"),
		LogLevel::INFO, LogCategory::Graphics);
	logger.Log(
		std::format("ユーザーリソース開始位置: RTV[{}], SRV[{}], DSV[{}]\n",
			kUserRTVStart, kUserSRVStart, kUserDSVStart),
		LogLevel::INFO, LogCategory::Graphics);
#endif
}

void DescriptorManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
	D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
	D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
	const std::string& debugName)
{
	assert(resource != nullptr && "Resource must not be null");

	// 境界チェック
	CheckDescriptorBounds(nextSRVDescriptorIndex_, kMaxSRVDescriptors, "SRV");

	// ハンドル計算
	CalculateSRVHandles(nextSRVDescriptorIndex_, outCpuDesc, outGpuDesc);

	// SRV作成
	device_->CreateShaderResourceView(resource, &desc, outCpuDesc);

	// ログ出力
	LogViewCreation(nextSRVDescriptorIndex_, "SRV", debugName);

	// インデックス更新
	++nextSRVDescriptorIndex_;
}

void DescriptorManager::CreateUAV(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
	D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
	D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
	const std::string& debugName)
{
	assert(resource != nullptr && "Resource must not be null");

	// 境界チェック
	CheckDescriptorBounds(nextSRVDescriptorIndex_, kMaxSRVDescriptors, "SRV/UAV");

	// ハンドル計算
	CalculateSRVHandles(nextSRVDescriptorIndex_, outCpuDesc, outGpuDesc);

	// UAV作成
	device_->CreateUnorderedAccessView(resource, nullptr, &desc, outCpuDesc);

	// ログ出力
	LogViewCreation(nextSRVDescriptorIndex_, "UAV", debugName);

	// インデックス更新
	++nextSRVDescriptorIndex_;
}

void DescriptorManager::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc,
	D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
	D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
	const std::string& debugName)
{
	// 境界チェック
	CheckDescriptorBounds(nextSRVDescriptorIndex_, kMaxSRVDescriptors, "CBV");

	// ハンドル計算
	CalculateSRVHandles(nextSRVDescriptorIndex_, outCpuDesc, outGpuDesc);

	// CBV作成
	device_->CreateConstantBufferView(&desc, outCpuDesc);

	// ログ出力
	LogViewCreation(nextSRVDescriptorIndex_, "CBV", debugName);

	// インデックス更新
	++nextSRVDescriptorIndex_;
}

void DescriptorManager::CreateRTV(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& rtvDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle, const std::string& debugName)
{
	assert(resource != nullptr && "Resource must not be null");

	// 境界チェック
	CheckDescriptorBounds(nextRTVDescriptorIndex_, kMaxRTVDescriptors, "RTV");

	// ハンドル計算
	outRtvHandle = CalculateRTVHandle(nextRTVDescriptorIndex_);

	// RTV作成
	device_->CreateRenderTargetView(resource, &rtvDesc, outRtvHandle);

	// ログ出力
	LogViewCreationWithCount(nextRTVDescriptorIndex_, "RTV", debugName, kMaxRTVDescriptors);

	// インデックス更新
	++nextRTVDescriptorIndex_;
}

void DescriptorManager::CreateDSV(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& dsvDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE& outDsvHandle, const std::string& debugName)
{
	assert(resource != nullptr && "Resource must not be null");

	// 境界チェック
	CheckDescriptorBounds(nextDSVDescriptorIndex_, kMaxDSVDescriptors, "DSV");

	// ハンドル計算
	outDsvHandle = CalculateDSVHandle(nextDSVDescriptorIndex_);

	// DSV作成
	device_->CreateDepthStencilView(resource, &dsvDesc, outDsvHandle);

	// ログ出力
	LogViewCreationWithCount(nextDSVDescriptorIndex_, "DSV", debugName, kMaxDSVDescriptors);

	// インデックス更新
	++nextDSVDescriptorIndex_;
}

void DescriptorManager::CreateDescriptorHeaps()
{
	// RTV用のディスクリプタヒープの生成
	rtvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kMaxRTVDescriptors, false);
	// SRV用のディスクリプタヒープの生成
	srvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVDescriptors, true);
	// DSV用のディスクリプタヒープの生成
	dsvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kMaxDSVDescriptors, false);
}

ComPtr<ID3D12DescriptorHeap> DescriptorManager::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors, bool shaderVisible)
{
	// ディスクリプタヒープの設定
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	ComPtr<ID3D12DescriptorHeap> descriptorHeap;
	HRESULT hr = device_->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(descriptorHeap.GetAddressOf()));

	// エラーチェック
	if (FAILED(hr)) {
		logger.Log(
			std::format("エラー: ディスクリプタヒープの作成に失敗しました! タイプ={}, 個数={}\n",
				static_cast<int>(heapType), numDescriptors),
			LogLevel::Error, LogCategory::Graphics);
		throw std::runtime_error("Failed to create descriptor heap");
	}

	return descriptorHeap;
}

void DescriptorManager::CheckDescriptorBounds(UINT currentIndex, UINT maxCount, const std::string& heapName)
{
	// 境界チェック
	if (currentIndex >= maxCount) {
		logger.Log(
			std::format("エラー: {}ヒープが満杯です! 最大数={}, 要求インデックス={}\n",
				heapName, maxCount, currentIndex),
			LogLevel::Error, LogCategory::Graphics);
		throw std::runtime_error(heapName + " descriptor heap is full!");
	}
}

void DescriptorManager::CalculateSRVHandles(UINT index,
	D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle,
	D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle)
{
	UINT descriptorSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// CPUハンドル計算
	outCpuHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
	outCpuHandle.ptr += index * descriptorSize;

	// GPUハンドル計算
	outGpuHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
	outGpuHandle.ptr += index * descriptorSize;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorManager::CalculateRTVHandle(UINT index)
{
	// RTVハンドル計算
	UINT rtvIncrementSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * rtvIncrementSize;
	return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorManager::CalculateDSVHandle(UINT index)
{
	// DSVハンドル計算
	UINT dsvIncrementSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += index * dsvIncrementSize;
	return handle;
}

void DescriptorManager::LogViewCreation(UINT index, const std::string& viewType, const std::string& debugName)
{
#ifdef _DEBUG
	float usageRate = GetSRVUsageRate() * 100.0f;
	logger.Log(
		std::format("{}[{}]に\"{}\"を登録しました (使用率: {:.1f}%)\n",
			viewType, index, debugName, usageRate),
		LogLevel::INFO, LogCategory::Graphics);

	// 警告閾値チェック
	if (usageRate > 80.0f) {
		logger.Log(
			std::format("警告: {}ヒープの使用率が高くなっています! ({:.1f}%)\n", viewType, usageRate),
			LogLevel::WARNING, LogCategory::Graphics);
	}
#else
	// リリースビルドでは未使用警告を抑制
	(void)index;
	(void)viewType;
	(void)debugName;
#endif
}

void DescriptorManager::LogViewCreationWithCount(UINT index, const std::string& viewType,
	const std::string& debugName, UINT maxCount)
{
#ifdef _DEBUG
	logger.Log(
		std::format("{}[{}]を作成しました: \"{}\" (使用数: {}/{})\n",
			viewType, index, debugName, index + 1, maxCount),
		LogLevel::INFO, LogCategory::Graphics);
#else
	// リリースビルドでは未使用警告を抑制
	(void)index;
	(void)viewType;
	(void)debugName;
	(void)maxCount;
#endif
}
}
