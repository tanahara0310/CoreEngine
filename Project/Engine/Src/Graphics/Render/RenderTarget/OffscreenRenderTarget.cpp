#include "pch.h"
#include "OffscreenRenderTarget.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Resource/ResourceFactory.h"

#include <format>
#include <cassert>

namespace CoreEngine
{
    OffscreenRenderTarget::~OffscreenRenderTarget()
    {
        ReleaseDescriptorHandles();
    }

    void OffscreenRenderTarget::Initialize(DirectXCommon* dx, DescriptorManager* descriptorManager, const RenderTargetDescriptor& desc, int index)
    {
        assert(dx);
        assert(descriptorManager);
        assert(index >= 0);

        dxCommon_ = dx;
        descriptorManager_ = descriptorManager;
        index_ = index;
        format_ = desc.format;
        useDepthBuffer_ = desc.needsDepthStencil;
        autoResize_ = desc.autoResize;
        SetClearColor(desc.clearColor);

        const uint32_t width = (desc.width > 0) ? desc.width : static_cast<uint32_t>(dx->GetClientWidth());
        const uint32_t height = (desc.height > 0) ? desc.height : static_cast<uint32_t>(dx->GetClientHeight());
        CreateOrResizeResource(width, height);
        CreateViews();
    }

    void OffscreenRenderTarget::Resize(uint32_t width, uint32_t height)
    {
        assert(dxCommon_);
        if (!autoResize_ || width == 0 || height == 0) {
            return;
        }

        CreateOrResizeResource(width, height);
        UpdateViews();
    }

    void OffscreenRenderTarget::CreateOrResizeResource(uint32_t width, uint32_t height)
    {
        assert(dxCommon_);

        width_ = static_cast<int32_t>(width);
        height_ = static_cast<int32_t>(height);

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = format_;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = texDesc.Format;
        clearValue.Color[0] = clearColor_[0];
        clearValue.Color[1] = clearColor_[1];
        clearValue.Color[2] = clearColor_[2];
        clearValue.Color[3] = clearColor_[3];

        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        resource_ = ResourceFactory::CreateTextureResource(
            device,
            texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue);

        currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    void OffscreenRenderTarget::CreateViews()
    {
        assert(dxCommon_);
        assert(descriptorManager_);
        assert(resource_);

        rtvDescriptor_ = descriptorManager_->AllocateRTVHandle(std::format("RenderTarget{}RTV", index_));
        srvDescriptor_ = descriptorManager_->AllocateSRVHandle(std::format("RenderTarget{}SRV", index_));
        uavDescriptor_ = descriptorManager_->AllocateSRVHandle(std::format("RenderTarget{}UAV", index_));

        UpdateViews();
    }

    void OffscreenRenderTarget::UpdateViews() const
    {
        assert(dxCommon_);
        assert(resource_);

        auto* device = dxCommon_->GetDevice();

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = format_;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(resource_.Get(), &rtvDesc, rtvDescriptor_.cpuHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format_;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(resource_.Get(), &srvDesc, srvDescriptor_.cpuHandle);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format_;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(resource_.Get(), nullptr, &uavDesc, uavDescriptor_.cpuHandle);

        dsvHandle_ = useCustomDsvHandle_ ? customDsvHandle_ : dxCommon_->GetDSVHandle();
    }

    void OffscreenRenderTarget::ReleaseDescriptorHandles()
    {
        if (!descriptorManager_) {
            return;
        }

        if (rtvDescriptor_.IsValid()) {
            descriptorManager_->Free(rtvDescriptor_);
        }
        if (srvDescriptor_.IsValid()) {
            descriptorManager_->Free(srvDescriptor_);
        }
        if (uavDescriptor_.IsValid()) {
            descriptorManager_->Free(uavDescriptor_);
        }
    }

    void OffscreenRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        dsvHandle_ = useCustomDsvHandle_ ? customDsvHandle_ : dxCommon_->GetDSVHandle();

        // 実際のリソース状態から RENDER_TARGET へ遷移（状態不一致によるチラつきを防ぐ）
        if (currentState_ != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            ResourceBarrierHelper::Transition(cmdList, resource_.Get(),
                currentState_,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        // RTV & DSV設定
        // useDepthBuffer_=false の場合（SSAOなどポストプロセス専用パス）は
        // 共有DSVをバインドしない。これによりGBufferPassが書き込んだ深度値を保護する。
        if (useDepthBuffer_) {
            cmdList->OMSetRenderTargets(1, &rtvDescriptor_.cpuHandle, false, &dsvHandle_);
        } else {
            cmdList->OMSetRenderTargets(1, &rtvDescriptor_.cpuHandle, false, nullptr);
        }

        // clearEnabled_=true のときのみRTVと深度をクリアする。
        // clearEnabled_=false は DeferredLightingPass が書き込んだ結果を
        // GeometryPass が上書きする際など、直前パスの内容を保持したい場合に使用する。
        if (clearEnabled_) {
            cmdList->ClearRenderTargetView(rtvDescriptor_.cpuHandle, clearColor_, 0, nullptr);
            if (useDepthBuffer_) {
                cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            }
        }

        // ビューポート設定
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &viewport);

        // シザー矩形設定
        D3D12_RECT scissor{};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = width_;
        scissor.bottom = height_;
        cmdList->RSSetScissorRects(1, &scissor);

        // SRVヒープ設定
        ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);
    }

    void OffscreenRenderTarget::End(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        // 実際のリソース状態から PIXEL_SHADER_RESOURCE へ遷移
        if (currentState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ResourceBarrierHelper::Transition(cmdList, resource_.Get(),
                currentState_,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            currentState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetUAVHandle() const
    {
        return uavDescriptor_.gpuHandle;
    }

    void OffscreenRenderTarget::BeginCS(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        if (currentState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            ResourceBarrierHelper::Transition(cmdList, resource_.Get(),
                currentState_,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            currentState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        // SRVヒープ設定（CS用バインドに必要）
        ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);
    }

    void OffscreenRenderTarget::EndCS(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        if (currentState_ != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            ResourceBarrierHelper::Transition(cmdList, resource_.Get(),
                currentState_,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            currentState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
    }

    void OffscreenRenderTarget::GetSize(int32_t& width, int32_t& height) const
    {
        width = width_;
        height = height_;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetRTVHandle() const
    {
        return rtvDescriptor_.cpuHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetSRVHandle() const
    {
        return srvDescriptor_.gpuHandle;
    }

    ID3D12Resource* OffscreenRenderTarget::GetResource() const
    {
        return resource_.Get();
    }

    int32_t OffscreenRenderTarget::GetWidth() const
    {
        return width_;
    }

    int32_t OffscreenRenderTarget::GetHeight() const
    {
        return height_;
    }

    void OffscreenRenderTarget::SetCurrentState(D3D12_RESOURCE_STATES state)
    {
        currentState_ = state;
    }

    D3D12_RESOURCE_STATES& OffscreenRenderTarget::GetCurrentState()
    {
        return currentState_;
    }

    D3D12_RESOURCE_STATES OffscreenRenderTarget::GetCurrentState() const
    {
        return currentState_;
    }
}
