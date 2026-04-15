#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <cstdint>
#include <stdexcept>

using namespace Microsoft::WRL;

/// @brief ディスクリプタヒープ管理クラス

namespace CoreEngine
{
class DescriptorManager {
public:
    // ディスクリプタヒープのデフォルト最大サイズ
    static constexpr UINT kDefaultMaxRTVDescriptors = 256;
    static constexpr UINT kDefaultMaxSRVDescriptors = 65536;
    static constexpr UINT kDefaultMaxDSVDescriptors = 10;

    // 予約済みインデックス（スワップチェーン用）
    static constexpr UINT kReservedSRVStart = 0;
    static constexpr UINT kReservedRTVStart = 0;
    static constexpr UINT kReservedDSVStart = 0;
    static constexpr UINT kUserSRVStart = 1; // ユーザーリソースは1から
    static constexpr UINT kUserRTVStart = 2;        // スワップチェーン用に0,1を予約
    static constexpr UINT kUserDSVStart = 0;        // DSVは0から使用可能

    /// @brief 初期化
    /// @param device D3D12デバイス
    /// @param maxSRV SRVディスクリプタ最大数
    /// @param maxRTV RTVディスクリプタ最大数
    /// @param maxDSV DSVディスクリプタ最大数
    void Initialize(ID3D12Device* device,
        UINT maxSRV = kDefaultMaxSRVDescriptors,
        UINT maxRTV = kDefaultMaxRTVDescriptors,
        UINT maxDSV = kDefaultMaxDSVDescriptors);

    /// @brief SRVの作成
    /// @param resource リソース
    /// @param desc SRV設定
    /// @param outCpuDesc CPUディスクリプタハンドル出力
    /// @param outGpuDesc GPUディスクリプタハンドル出力
    /// @param debugName デバッグ用名前
    void CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
        const std::string& debugName = "Unknown");

    /// @brief UAVの作成
    /// @param resource リソース
    /// @param desc UAV設定
    /// @param outCpuDesc CPUディスクリプタハンドル出力
    /// @param outGpuDesc GPUディスクリプタハンドル出力
    /// @param debugName デバッグ用名前
    void CreateUAV(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
        const std::string& debugName = "Unknown");

    /// @brief CBVの作成
    /// @param desc CBV設定
    /// @param outCpuDesc CPUディスクリプタハンドル出力
    /// @param outGpuDesc GPUディスクリプタハンドル出力
    /// @param debugName デバッグ用名前
    void CreateCBV(const D3D12_CONSTANT_BUFFER_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
        const std::string& debugName = "Unknown");

    /// @brief RTVの作成
    /// @param resource リソース
    /// @param rtvDesc RTV設定
    /// @param outRtvHandle RTVハンドル出力
    /// @param debugName デバッグ用名前
    void CreateRTV(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& rtvDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle, const std::string& debugName = "Unknown");

    /// @brief DSVの作成
    /// @param resource リソース
    /// @param dsvDesc DSV設定
    /// @param outDsvHandle DSVハンドル出力
    /// @param debugName デバッグ用名前
    void CreateDSV(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC& dsvDesc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outDsvHandle, const std::string& debugName = "Unknown");

    // アクセッサ
    ID3D12DescriptorHeap* GetRTVHeap() const { return rtvHeap_.Get(); }
    ID3D12DescriptorHeap* GetSRVHeap() const { return srvHeap_.Get(); }
    ID3D12DescriptorHeap* GetDSVHeap() const { return dsvHeap_.Get(); }

    // 使用状況の取得
    UINT GetUsedSRVCount() const { return nextSRVDescriptorIndex_; }
    UINT GetUsedRTVCount() const { return nextRTVDescriptorIndex_; }
    UINT GetUsedDSVCount() const { return nextDSVDescriptorIndex_; }
    UINT GetMaxSRVDescriptors() const { return maxSRVDescriptors_; }
    UINT GetMaxRTVDescriptors() const { return maxRTVDescriptors_; }
    UINT GetMaxDSVDescriptors() const { return maxDSVDescriptors_; }
    float GetSRVUsageRate() const { return static_cast<float>(nextSRVDescriptorIndex_) / maxSRVDescriptors_; }
    float GetDSVUsageRate() const { return static_cast<float>(nextDSVDescriptorIndex_) / maxDSVDescriptors_; }

private:
    /// @brief ディスクリプタヒープの生成
    void CreateDescriptorHeaps();

    /// @brief ディスクリプタヒープ作成のヘルパー関数
    /// @param heapType ヒープタイプ
    /// @param numDescriptors ディスクリプタ数
    /// @param shaderVisible シェーダーから見えるか
    /// @return 作成されたディスクリプタヒープ
    ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
        UINT numDescriptors, bool shaderVisible);

    /// @brief ディスクリプタの境界チェック
    /// @param currentIndex 現在のインデックス
    /// @param maxCount 最大数
    /// @param heapName ヒープ名（エラーメッセージ用）
    void CheckDescriptorBounds(UINT currentIndex, UINT maxCount, const std::string& heapName);

    /// @brief CBV/SRV/UAV用のディスクリプタハンドルを計算
    /// @param index インデックス
    /// @param outCpuHandle CPU出力ハンドル
    /// @param outGpuHandle GPU出力ハンドル
    void CalculateSRVHandles(UINT index,
        D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle);

    /// @brief RTV用のディスクリプタハンドルを計算
    /// @param index インデックス
    /// @return CPUハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE CalculateRTVHandle(UINT index);

    /// @brief DSV用のディスクリプタハンドルを計算
    /// @param index インデックス
    /// @return CPUハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE CalculateDSVHandle(UINT index);

    /// @brief デバッグログ出力（SRV/UAV/CBV用）
    /// @param index インデックス
    /// @param viewType ビュータイプ（"SRV", "UAV", "CBV"）
    /// @param debugName リソース名
    void LogViewCreation(UINT index, const std::string& viewType, const std::string& debugName);

    /// @brief デバッグログ出力（RTV/DSV用）
    /// @param index インデックス
    /// @param viewType ビュータイプ（"RTV", "DSV"）
    /// @param debugName リソース名
    /// @param maxCount 最大数
    void LogViewCreationWithCount(UINT index, const std::string& viewType,
        const std::string& debugName, UINT maxCount);

private:
    // ディスクプリタヒープ
    ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    ComPtr<ID3D12DescriptorHeap> srvHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvHeap_;

    // ディスクリプタヒープの最大サイズ（コンフィグから取得）
    UINT maxSRVDescriptors_ = kDefaultMaxSRVDescriptors;
    UINT maxRTVDescriptors_ = kDefaultMaxRTVDescriptors;
    UINT maxDSVDescriptors_ = kDefaultMaxDSVDescriptors;

    // 次に割り当てるディスクリプタのインデックス
    uint32_t nextSRVDescriptorIndex_ = kUserSRVStart;
    uint32_t nextRTVDescriptorIndex_ = kUserRTVStart;
    uint32_t nextDSVDescriptorIndex_ = kUserDSVStart;

    ID3D12Device* device_ = nullptr;
};
}
