#include "DescriptorManager.h"
#include "Utility/Logger/Logger.h"

#include <cassert>

using namespace Microsoft::WRL;


namespace CoreEngine
{
namespace {
    Logger& logger = Logger::GetInstance();
}

void DescriptorManager::Initialize(ID3D12Device* device, UINT maxSRV, UINT maxRTV, UINT maxDSV)
{
    assert(device != nullptr && "Device must not be null");
    device_ = device;
    maxSRVDescriptors_ = maxSRV;
    maxRTVDescriptors_ = maxRTV;
    maxDSVDescriptors_ = maxDSV;
    CreateDescriptorHeaps();

    // DescriptorHandleIncrementSize をキャッシュ（毎回 API を叩かないようにする）
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "DescriptorManager初期化完了: SRV最大数={}, RTV最大数={}, DSV最大数={}\n",
        maxSRVDescriptors_, maxRTVDescriptors_, maxDSVDescriptors_);
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "DescriptorIncrementSize: SRV/CBV/UAV={}, RTV={}, DSV={}\n",
        srvDescriptorSize_, rtvDescriptorSize_, dsvDescriptorSize_);

#ifdef _DEBUG
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "予約済みディスクリプタ: RTV[0-1]=スワップチェーン, SRV[0]=ImGui用, DSV[なし]\n");
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "ユーザーリソース開始位置: RTV[{}], SRV[{}], DSV[{}]\n",
        kUserRTVStart, kUserSRVStart, kUserDSVStart);
#endif
}

void DescriptorManager::CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
    D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
    const std::string& debugName)
{
    // resource == nullptr は TLAS SRV (RAYTRACING_ACCELERATION_STRUCTURE) で正当
    assert((resource != nullptr
        || desc.ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE)
        && "Resource must not be null (except for Raytracing Acceleration Structure SRV)");

    const UINT index = AllocateSRVIndex();

    // ハンドル計算
    CalculateSRVHandles(index, outCpuDesc, outGpuDesc);

    // SRV作成
    device_->CreateShaderResourceView(resource, &desc, outCpuDesc);

    // ログ出力
    LogViewCreation(index, "SRV", debugName);
}

void DescriptorManager::CreateUAV(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
    D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
    const std::string& debugName)
{
    assert(resource != nullptr && "Resource must not be null");

    const UINT index = AllocateSRVIndex();

    // ハンドル計算
    CalculateSRVHandles(index, outCpuDesc, outGpuDesc);

    // UAV作成
    device_->CreateUnorderedAccessView(resource, nullptr, &desc, outCpuDesc);

    // ログ出力
    LogViewCreation(index, "UAV", debugName);
}

void DescriptorManager::CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
    D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
    const std::string& debugName)
{
    const UINT index = AllocateSRVIndex();

    // ハンドル計算
    CalculateSRVHandles(index, outCpuDesc, outGpuDesc);

    // CBV作成
    device_->CreateConstantBufferView(&desc, outCpuDesc);

    // ログ出力
    LogViewCreation(index, "CBV", debugName);
}

void DescriptorManager::CreateRTV(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& rtvDesc,
    D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle, const std::string& debugName)
{
    assert(resource != nullptr && "Resource must not be null");

    const UINT index = AllocateRTVIndex();

    // ハンドル計算
    outRtvHandle = CalculateRTVHandle(index);

    // RTV作成
    device_->CreateRenderTargetView(resource, &rtvDesc, outRtvHandle);

    // ログ出力
    LogViewCreationWithCount(index, "RTV", debugName, maxRTVDescriptors_);
}

void DescriptorManager::CreateDSV(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& dsvDesc,
    D3D12_CPU_DESCRIPTOR_HANDLE& outDsvHandle, const std::string& debugName)
{
    assert(resource != nullptr && "Resource must not be null");

    const UINT index = AllocateDSVIndex();

    // ハンドル計算
    outDsvHandle = CalculateDSVHandle(index);

    // DSV作成
    device_->CreateDepthStencilView(resource, &dsvDesc, outDsvHandle);

    // ログ出力
    LogViewCreationWithCount(index, "DSV", debugName, maxDSVDescriptors_);
}

void DescriptorManager::CreateDescriptorHeaps()
{
    // RTV用のディスクリプタヒープの生成
    rtvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, maxRTVDescriptors_, false);
    // SRV用のディスクリプタヒープの生成
    srvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, maxSRVDescriptors_, true);
    // DSV用のディスクリプタヒープの生成
    dsvHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, maxDSVDescriptors_, false);
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DescriptorManager::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors, bool shaderVisible)
{
    // ディスクリプタヒープの設定
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    HRESULT hr = device_->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(descriptorHeap.GetAddressOf()));

    // エラーチェック
    if (FAILED(hr)) {
        logger.Errorf(LogCategory::Graphics, LogSubCategory::Heap,
            "エラー: ディスクリプタヒープの作成に失敗しました! タイプ={}, 個数={}\n",
            static_cast<int>(heapType), numDescriptors);
        throw std::runtime_error("Failed to create descriptor heap");
    }

    return descriptorHeap;
}

void DescriptorManager::CheckDescriptorBounds(UINT currentIndex, UINT maxCount, const std::string& heapName)
{
    // 境界チェック
    if (currentIndex >= maxCount) {
        logger.Errorf(LogCategory::Graphics, LogSubCategory::Heap,
            "エラー: {}ヒープが満杯です! 最大数={}, 要求インデックス={}\n",
            heapName, maxCount, currentIndex);
        throw std::runtime_error(heapName + " descriptor heap is full!");
    }
}

void DescriptorManager::CalculateSRVHandles(UINT index,
    D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle,
    D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle)
{
    // CPUハンドル計算（キャッシュ済みサイズを使用）
    outCpuHandle = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    outCpuHandle.ptr += index * srvDescriptorSize_;

    // GPUハンドル計算
    outGpuHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    outGpuHandle.ptr += index * srvDescriptorSize_;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorManager::CalculateRTVHandle(UINT index)
{
    // RTVハンドル計算（キャッシュ済みサイズを使用）
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * rtvDescriptorSize_;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorManager::CalculateDSVHandle(UINT index)
{
    // DSVハンドル計算（キャッシュ済みサイズを使用）
    D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * dsvDescriptorSize_;
    return handle;
}

void DescriptorManager::LogViewCreation(UINT index, const std::string& viewType, const std::string& debugName)
{
#ifdef _DEBUG
    float usageRate = GetSRVUsageRate() * 100.0f;
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "{}[{}]に\"{}\"を登録しました (使用率: {:.1f}%)\n",
        viewType, index, debugName, usageRate);

    // 警告閾値チェック
    if (usageRate > 80.0f) {
        logger.Warnf(LogCategory::Graphics, LogSubCategory::Heap,
            "警告: {}ヒープの使用率が高くなっています! ({:.1f}%)\n", viewType, usageRate);
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
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "{}[{}]を作成しました: \"{}\" (使用数: {}/{})\n",
        viewType, index, debugName, index + 1, maxCount);
#else
    // リリースビルドでは未使用警告を抑制
    (void)index;
    (void)viewType;
    (void)debugName;
    (void)maxCount;
#endif
}

// ---------------------------------------------------------------
// AllocateIndex / FreeIndex（フリーリスト）
// ---------------------------------------------------------------

UINT DescriptorManager::AllocateSRVIndex()
{
    if (!freeSRVIndices_.empty()) {
        UINT index = freeSRVIndices_.back();
        freeSRVIndices_.pop_back();
        logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
            "SRV/CBV/UAVスロット再利用: index={} (フリーリスト残り={})\n",
            index, freeSRVIndices_.size());
        return index;
    }
    CheckDescriptorBounds(nextSRVDescriptorIndex_, maxSRVDescriptors_, "SRV/CBV/UAV");
    return nextSRVDescriptorIndex_++;
}

UINT DescriptorManager::AllocateRTVIndex()
{
    if (!freeRTVIndices_.empty()) {
        UINT index = freeRTVIndices_.back();
        freeRTVIndices_.pop_back();
        logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
            "RTVスロット再利用: index={} (フリーリスト残り={})\n",
            index, freeRTVIndices_.size());
        return index;
    }
    CheckDescriptorBounds(nextRTVDescriptorIndex_, maxRTVDescriptors_, "RTV");
    return nextRTVDescriptorIndex_++;
}

UINT DescriptorManager::AllocateDSVIndex()
{
    if (!freeDSVIndices_.empty()) {
        UINT index = freeDSVIndices_.back();
        freeDSVIndices_.pop_back();
        logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
            "DSVスロット再利用: index={} (フリーリスト残り={})\n",
            index, freeDSVIndices_.size());
        return index;
    }
    CheckDescriptorBounds(nextDSVDescriptorIndex_, maxDSVDescriptors_, "DSV");
    return nextDSVDescriptorIndex_++;
}

void DescriptorManager::FreeSRVIndex(UINT index)
{
    assert(index < nextSRVDescriptorIndex_ && "解放するSRVインデックスが範囲外です");
    freeSRVIndices_.push_back(index);
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "SRV/CBV/UAVスロット解放: index={} (フリーリスト数={})\n"
        "  ※ GPU同期後に呼ばれていることを確認してください\n",
        index, freeSRVIndices_.size());
}

void DescriptorManager::FreeRTVIndex(UINT index)
{
    assert(index < nextRTVDescriptorIndex_ && "解放するRTVインデックスが範囲外です");
    freeRTVIndices_.push_back(index);
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "RTVスロット解放: index={} (フリーリスト数={})\n"
        "  ※ GPU同期後に呼ばれていることを確認してください\n",
        index, freeRTVIndices_.size());
}

void DescriptorManager::FreeDSVIndex(UINT index)
{
    assert(index < nextDSVDescriptorIndex_ && "解放するDSVインデックスが範囲外です");
    freeDSVIndices_.push_back(index);
    logger.Infof(LogCategory::Graphics, LogSubCategory::Heap,
        "DSVスロット解放: index={} (フリーリスト数={})\n"
        "GPU同期後に呼ばれていることを確認してください\n",
        index, freeDSVIndices_.size());
}

// ---------------------------------------------------------------
// CPUハンドル → スロットインデックス逆算
// ---------------------------------------------------------------

UINT DescriptorManager::GetSRVIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const
{
    const SIZE_T base = srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
    assert(handle.ptr >= base && srvDescriptorSize_ > 0);
    return static_cast<UINT>((handle.ptr - base) / srvDescriptorSize_);
}

UINT DescriptorManager::GetRTVIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const
{
    const SIZE_T base = rtvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
    assert(handle.ptr >= base && rtvDescriptorSize_ > 0);
    return static_cast<UINT>((handle.ptr - base) / rtvDescriptorSize_);
}

UINT DescriptorManager::GetDSVIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const
{
    const SIZE_T base = dsvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
    assert(handle.ptr >= base && dsvDescriptorSize_ > 0);
    return static_cast<UINT>((handle.ptr - base) / dsvDescriptorSize_);
}
}
