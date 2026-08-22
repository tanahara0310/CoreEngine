#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <atomic>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"

namespace CoreEngine
{
/// @brief ディスクリプタヒープ管理クラス
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

    /// @brief SRV の作成
    void CreateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
        const std::string& debugName = "Unknown");

    /// @brief UAV の作成
    void CreateUAV(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& outCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& outGpuDesc,
        const std::string& debugName = "Unknown");

    /// @brief SRV の作成または更新
    /// @details ioCpuDesc が未確保（ptr==0）なら新規スロットを確保し、確保済みなら同じスロットへ書き直す。
    ///          リサイズ等でリソースを再作成する側がスロットをリークさせないための API
    void CreateOrUpdateSRV(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& ioCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& ioGpuDesc,
        const std::string& debugName = "Unknown");

    /// @brief UAV の作成または更新
    /// @details ioCpuDesc が未確保（ptr==0）なら新規スロットを確保し、確保済みなら同じスロットへ書き直す
    void CreateOrUpdateUAV(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE& ioCpuDesc,
        D3D12_GPU_DESCRIPTOR_HANDLE& ioGpuDesc,
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

    // ===== DescriptorHandle ベースの安全な Allocate/Free API =====

    /// @brief SRV/CBV/UAV スロットを確保して DescriptorHandle を返す
    /// @param debugName デバッグ用名前
    DescriptorHandle AllocateSRVHandle(const std::string& debugName = "Unknown");

    /// @brief RTV スロットを確保して DescriptorHandle を返す
    /// @param debugName デバッグ用名前
    DescriptorHandle AllocateRTVHandle(const std::string& debugName = "Unknown");

    /// @brief DSV スロットを確保して DescriptorHandle を返す
    /// @param debugName デバッグ用名前
    DescriptorHandle AllocateDSVHandle(const std::string& debugName = "Unknown");

    /// @brief DescriptorHandle を解放してフリーリストへ返す
    /// @details ヒープ種別・二重解放・範囲外を自動検証する
    /// @note GPUがそのスロットを参照し終えた後（フェンス完了後）に呼ぶこと
    /// @param handle 解放するハンドル（呼び出し後は Invalidate される）
    void Free(DescriptorHandle& handle);

    // アクセッサ
    ID3D12DescriptorHeap* GetRTVHeap() const { return rtvHeap_.Get(); }
    ID3D12DescriptorHeap* GetSRVHeap() const { return srvHeap_.Get(); }
    ID3D12DescriptorHeap* GetDSVHeap() const { return dsvHeap_.Get(); }

    // 使用状況の取得
    UINT GetUsedSRVCount() const { return nextSRVDescriptorIndex_.load(); }
    UINT GetUsedRTVCount() const { return nextRTVDescriptorIndex_.load(); }
    UINT GetUsedDSVCount() const { return nextDSVDescriptorIndex_.load(); }
    UINT GetMaxSRVDescriptors() const { return maxSRVDescriptors_; }
    UINT GetMaxRTVDescriptors() const { return maxRTVDescriptors_; }
    UINT GetMaxDSVDescriptors() const { return maxDSVDescriptors_; }
    float GetSRVUsageRate() const { return static_cast<float>(nextSRVDescriptorIndex_.load()) / maxSRVDescriptors_; }
    float GetDSVUsageRate() const { return static_cast<float>(nextDSVDescriptorIndex_.load()) / maxDSVDescriptors_; }

    /// @brief CBV/SRV/UAVスロットを解放してフリーリストに返す
    /// @note GPUがそのスロットを参照し終わった後（フェンス完了後）に呼ぶこと
    /// @param index 解放するスロットインデックス
    void FreeSRVIndex(UINT index);

    /// @brief RTVスロットを解放してフリーリストに返す
    /// @note GPUがそのスロットを参照し終わった後（フェンス完了後）に呼ぶこと
    /// @param index 解放するスロットインデックス
    void FreeRTVIndex(UINT index);

    /// @brief DSVスロットを解放してフリーリストに返す
    /// @note GPUがそのスロットを参照し終わった後（フェンス完了後）に呼ぶこと
    /// @param index 解放するスロットインデックス
    void FreeDSVIndex(UINT index);

    /// @brief CBV/SRV/UAV CPUハンドルからスロットインデックスを逆算する
    UINT GetSRVIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

    /// @brief RTV CPUハンドルからスロットインデックスを逆算する
    UINT GetRTVIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

    /// @brief DSV CPUハンドルからスロットインデックスを逆算する
    UINT GetDSVIndexFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

private:
    /// @brief ディスクリプタヒープの生成
    void CreateDescriptorHeaps();

    /// @brief ディスクリプタヒープ作成のヘルパー関数
    /// @param heapType ヒープタイプ
    /// @param numDescriptors ディスクリプタ数
    /// @param shaderVisible シェーダーから見えるか
    /// @return 作成されたディスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
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
    // ディスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

    // フリーリスト（解放済みスロットの再利用）
    std::vector<UINT> freeSRVIndices_;
    std::vector<UINT> freeRTVIndices_;
    std::vector<UINT> freeDSVIndices_;

    // DescriptorHandleIncrementSize キャッシュ（毎フレーム呼び出しを避けるため）
    UINT srvDescriptorSize_ = 0;
    UINT rtvDescriptorSize_ = 0;
    UINT dsvDescriptorSize_ = 0;

    // ディスクリプタヒープの最大サイズ（コンフィグから取得）
    UINT maxSRVDescriptors_ = kDefaultMaxSRVDescriptors;
    UINT maxRTVDescriptors_ = kDefaultMaxRTVDescriptors;
    UINT maxDSVDescriptors_ = kDefaultMaxDSVDescriptors;

    // 次に割り当てるディスクリプタのインデックス（複数スレッドから読まれるため atomic）
    std::atomic<uint32_t> nextSRVDescriptorIndex_{ kUserSRVStart };
    std::atomic<uint32_t> nextRTVDescriptorIndex_{ kUserRTVStart };
    std::atomic<uint32_t> nextDSVDescriptorIndex_{ kUserDSVStart };

    ID3D12Device* device_ = nullptr;

    // ヒープ別ミューテックス（Allocate/Free の排他制御）
    mutable std::mutex srvMutex_;
    mutable std::mutex rtvMutex_;
    mutable std::mutex dsvMutex_;

#ifdef _DEBUG
    // 二重解放検出用（_DEBUG ビルドのみ有効）
    std::vector<bool> allocatedSRVSlots_;
    std::vector<bool> allocatedRTVSlots_;
    std::vector<bool> allocatedDSVSlots_;
#endif

    /// @brief CBV/SRV/UAV ヒープのスロットを確保して返す（フリーリスト優先）
    UINT AllocateSRVIndex();

    /// @brief RTV ヒープのスロットを確保して返す（フリーリスト優先）
    UINT AllocateRTVIndex();

    /// @brief DSV ヒープのスロットを確保して返す（フリーリスト優先）
    UINT AllocateDSVIndex();
};
}
