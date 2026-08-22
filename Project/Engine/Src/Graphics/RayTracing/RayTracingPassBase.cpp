#include "pch.h"
#include "RayTracingPassBase.h"

#include <cstring>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    // 派生から呼ぶ共通初期化。デバイス・出力ビュー集合・デバッグ名だけを受け取る
    bool RayTracingPassBase::InitializeBase(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr,
        const char* ownerName,
        const char* outputDebugName)
    {
        ownerName_ = ownerName ? ownerName : "RayTracingPassBase";
        outputDebugName_ = outputDebugName ? outputDebugName : "RTOutput";

        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        asMgr_ = asMgr;

        if (!dxCommon_ || !descriptorAllocator_ || !asMgr_ || !asMgr_->IsSupported()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: DXR unsupported. initialization skipped.",
                ownerName_);
            return false;
        }

        return true;
    }

    bool RayTracingPassBase::EnsureConstantBuffer(UINT bufferSize)
    {
        // 1 度作ったら使い回す。UPLOAD ヒープなので毎フレーム CPU から直接書き換えられる
        if (constantBuffer_) {
            return true;
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bufferSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&constantBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: constant buffer allocation failed. hr={:#x}",
                ownerName_,
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange = { 0, 0 };
        hr = constantBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&constantBufferMapped_));
        if (FAILED(hr) || !constantBufferMapped_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: constant buffer map failed. hr={:#x}",
                ownerName_,
                static_cast<uint32_t>(hr));
            constantBuffer_.Reset();
            constantBufferMapped_ = nullptr;
            return false;
        }

        std::memset(constantBufferMapped_, 0, bufferSize);
        return true;
    }

    // view ごとの出力テクスチャを必要サイズで用意する（サイズ据え置きなら何もしない）
    bool RayTracingPassBase::EnsureOutputTextureBase(
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DXGI_FORMAT format)
    {
        RayTracingOutputViewSet::TextureOptions options{};
        options.format = format;
        options.allowUAV = true;
        options.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        return outputViews_.EnsureTexture(
            dxCommon_,
            descriptorAllocator_,
            width,
            height,
            viewIndex,
            ownerName_,
            std::string(outputDebugName_) + "_v" + std::to_string(viewIndex),
            options);
    }

    // 画面サイズが変わったときだけ出力テクスチャを捨てる（次の Ensure で作り直される）
    void RayTracingPassBase::ReleaseOutputIfSizeMismatchBase(UINT width, UINT height, uint32_t viewIndex)
    {
        outputViews_.ReleaseIfSizeMismatch(width, height, viewIndex);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingPassBase::GetOutputSRVHandleBase(uint32_t viewIndex) const
    {
        return outputViews_.GetSRVHandle(viewIndex);
    }

    ID3D12Resource* RayTracingPassBase::GetOutputResourceBase(uint32_t viewIndex) const
    {
        return outputViews_.GetResource(viewIndex);
    }

    D3D12_RESOURCE_STATES& RayTracingPassBase::GetOutputCurrentStateBase(uint32_t viewIndex)
    {
        return outputViews_.GetCurrentState(viewIndex);
    }

    // ディスパッチ可能かを判定する。DXR オブジェクト・出力テクスチャ・
    // TLAS のどれが欠けているかを DispatchGuardStatus で返す
    RayTracingPassBase::DispatchGuardStatus RayTracingPassBase::ValidateDispatchPreconditions(
        ID3D12GraphicsCommandList* cmdList) const
    {
        if (!isInitialized_) {
            return DispatchGuardStatus::NotInitialized;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            return DispatchGuardStatus::RayTracingUnsupported;
        }
        if (asMgr_->GetBLASCount() == 0) {
            return DispatchGuardStatus::NoBLAS;
        }
        if (!cmdList) {
            return DispatchGuardStatus::InvalidCommandList;
        }
        return DispatchGuardStatus::Ok;
    }

    bool RayTracingPassBase::QueryCommandList4(
        ID3D12GraphicsCommandList* cmdList,
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4>& cmdList4) const
    {
        cmdList4.Reset();
        if (!cmdList) {
            return false;
        }

        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "{}: QueryInterface(ID3D12GraphicsCommandList4) failed.",
                ownerName_);
            return false;
        }
        return true;
    }

    // ガード失敗の理由を 1 度だけ警告ログへ出す
    // （毎フレーム同じ警告が流れると本当のエラーが埋もれるため、状態変化時のみ）
    void RayTracingPassBase::ReportDispatchGuardFailure(
        DispatchGuardStatus guardStatus,
        ID3D12GraphicsCommandList* cmdList)
    {
        switch (guardStatus) {
        case DispatchGuardStatus::NotInitialized:
            lastDispatchInfo_.status = RayTracingDispatchStatus::NotInitialized;
            break;
        case DispatchGuardStatus::RayTracingUnsupported:
            lastDispatchInfo_.status = RayTracingDispatchStatus::RayTracingUnsupported;
            break;
        case DispatchGuardStatus::NoBLAS:
            lastDispatchInfo_.status = RayTracingDispatchStatus::NoBLAS;
            break;
        case DispatchGuardStatus::InvalidCommandList:
        case DispatchGuardStatus::CommandList4Unavailable:
            lastDispatchInfo_.status = RayTracingDispatchStatus::CommandList4Unavailable;
            break;
        case DispatchGuardStatus::OutputAllocationFailed:
            lastDispatchInfo_.status = RayTracingDispatchStatus::OutputAllocationFailed;
            break;
        default:
            lastDispatchInfo_.status = RayTracingDispatchStatus::RayTracingUnsupported;
            break;
        }

        Logger::GetInstance().Warnf(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "{}: dispatch skipped. status={} initialized={} cmdList={} asMgr={} supported={}",
            ownerName_,
            ToString(lastDispatchInfo_.status),
            isInitialized_,
            cmdList != nullptr,
            asMgr_ != nullptr,
            asMgr_ ? asMgr_->IsSupported() : false);
    }

    // 今回のディスパッチ内容を診断情報へ記録する（デバッグ UI が読む）
    void RayTracingPassBase::BeginDiagnosticsBase(uint32_t viewIndex, UINT width, UINT height)
    {
        lastDispatchInfo_ = {};
        lastDispatchInfo_.passName = ownerName_;
        lastDispatchInfo_.viewIndex = viewIndex;
        lastDispatchInfo_.width = width;
        lastDispatchInfo_.height = height;
        lastDispatchInfo_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;
    }

    // ディスパッチ前の共通処理をまとめて行う入口。
    // ここが false を返したら、派生側は何もせず即 return してよい（ログは出し済み）
    bool RayTracingPassBase::BeginDispatchBase(
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DispatchResources& outResources,
        DXGI_FORMAT format,
        UINT constantBufferSize)
    {
        DispatchGuardStatus guardStatus = ValidateDispatchPreconditions(cmdList);
        if (guardStatus == DispatchGuardStatus::Ok) {
            if (!EnsureOutputTextureBase(width, height, viewIndex, format)) {
                guardStatus = DispatchGuardStatus::OutputAllocationFailed;
            } else if (constantBufferSize != 0 && !EnsureConstantBuffer(constantBufferSize)) {
                guardStatus = DispatchGuardStatus::OutputAllocationFailed;
            } else if (!QueryCommandList4(cmdList, outResources.cmdList4)) {
                guardStatus = DispatchGuardStatus::CommandList4Unavailable;
            }
        }

        if (guardStatus != DispatchGuardStatus::Ok) {
            ReportDispatchGuardFailure(guardStatus, cmdList);
            return false;
        }

        outResources.outputSrvHandle = GetOutputSRVHandleBase(viewIndex);
        outResources.outputUavHandle = outputViews_.GetUAVHandle(viewIndex);
        outResources.outputResource = GetOutputResourceBase(viewIndex);
        outResources.outputCurrentState = &GetOutputCurrentStateBase(viewIndex);
        lastDispatchInfo_.outputSrvPtr = outResources.outputSrvHandle.ptr;
        return true;
    }

    // 出力テクスチャを UNORDERED_ACCESS へ遷移させる（DispatchRays の直前に呼ぶ）
    void RayTracingPassBase::BeginOutputWrite(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputCurrentState) const
    {
        ResourceBarrierHelper::Transition(
            cmdList,
            outputResource,
            outputCurrentState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        outputCurrentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    void RayTracingPassBase::EndOutputWrite(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputCurrentState,
        D3D12_RESOURCE_STATES finalState) const
    {
        ResourceBarrierHelper::UAV(cmdList, outputResource);
        ResourceBarrierHelper::Transition(
            cmdList,
            outputResource,
            outputCurrentState,
            finalState);
        outputCurrentState = finalState;
    }
}
