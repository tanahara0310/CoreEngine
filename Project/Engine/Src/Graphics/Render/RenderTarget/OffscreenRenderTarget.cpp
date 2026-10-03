#include "pch.h"
#include "OffscreenRenderTarget.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"

#include <algorithm>
#include <format>
#include <cassert>

namespace CoreEngine
{
    void OffscreenRenderTarget::Initialize(GraphicsCore* dx, DescriptorAllocator* descriptorAllocator, SceneDepth* sharedDepth,
                                           const RenderTargetDescriptor& desc, int index)
    {
        assert(dx);
        assert(descriptorAllocator);
        assert(index >= 0);

        dxCommon_ = dx;
        descriptorAllocator_ = descriptorAllocator;
        sharedDepth_ = sharedDepth;
        index_ = index;
        format_ = desc.format;
        needsDepthStencil_ = desc.needsDepthStencil;
        autoResize_ = desc.autoResize;
        SetClearColor(desc.clearColor);

        const uint32_t width = (desc.width > 0)
            ? desc.width
            : std::max(1u, static_cast<uint32_t>(dx->GetClientWidth() * desc.resolutionScale));
        const uint32_t height = (desc.height > 0)
            ? desc.height
            : std::max(1u, static_cast<uint32_t>(dx->GetClientHeight() * desc.resolutionScale));
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
        resource_.Reset(
            ResourceFactory::CreateTextureResource(
                device,
                texDesc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &clearValue),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    void OffscreenRenderTarget::CreateViews()
    {
        assert(dxCommon_);
        assert(descriptorAllocator_);
        assert(resource_);

        rtvDescriptor_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->AllocateRTVHandle(std::format("RenderTarget{}RTV", index_)));
        srvDescriptor_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->AllocateSRVHandle(std::format("RenderTarget{}SRV", index_)));
        uavDescriptor_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->AllocateSRVHandle(std::format("RenderTarget{}UAV", index_)));

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
        device->CreateRenderTargetView(resource_.Get(), &rtvDesc, rtvDescriptor_.Cpu());

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format_;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(resource_.Get(), &srvDesc, srvDescriptor_.Cpu());

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format_;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(resource_.Get(), nullptr, &uavDesc, uavDescriptor_.Cpu());
    }

    D3D12_CPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::ResolveDsvHandle() const
    {
        // 共有シーン深度の DSV スロットはリサイズしても変わらない（同じスロットへ書き直される）
        return sharedDepth_ ? sharedDepth_->GetDSVHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }

    RenderTargetBinding OffscreenRenderTarget::Begin(ID3D12GraphicsCommandList* cmdList, const RenderTargetBeginDesc& desc)
    {
        assert(cmdList);
        assert(resource_);

        // 深度は深度ありで作ったものだけ束ねる。束ねないとき（ポストプロセスなど）は共有のシーン深度に触れない
        const bool bindDepth = needsDepthStencil_ && desc.depth == DepthBinding::FromDescriptor;
        RenderTargetBinding binding;
        binding.rtv = rtvDescriptor_.Cpu();
        if (bindDepth) {
            binding.dsv = ResolveDsvHandle();
            assert(binding.dsv.ptr != 0 && "OffscreenRenderTarget: 深度を使うのに DSV が無い");
        }

        // 実際のリソース状態から RENDER_TARGET へ遷移（状態不一致によるチラつきを防ぐ）
        Barrier::Transition(cmdList, resource_, D3D12_RESOURCE_STATE_RENDER_TARGET);

        cmdList->OMSetRenderTargets(1, &binding.rtv, false, bindDepth ? &binding.dsv : nullptr);

        // クリアしないのは、直前のパスの内容を残したいとき（DeferredLighting の結果へ GeometryPass が重ねるなど）
        if (desc.clear) {
            cmdList->ClearRenderTargetView(binding.rtv, desc.clearColor ? desc.clearColor : clearColor_, 0, nullptr);
            if (bindDepth) {
                cmdList->ClearDepthStencilView(binding.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
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

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）
        return binding;
    }

    void OffscreenRenderTarget::End(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        // 実際のリソース状態から PIXEL_SHADER_RESOURCE へ遷移
        Barrier::Transition(cmdList, resource_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetUAVHandle() const
    {
        return uavDescriptor_.Gpu();
    }

    void OffscreenRenderTarget::BeginCS(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        Barrier::Transition(cmdList, resource_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）
    }

    void OffscreenRenderTarget::EndCS(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(resource_);

        Barrier::Transition(cmdList, resource_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void OffscreenRenderTarget::GetSize(int32_t& width, int32_t& height) const
    {
        width = width_;
        height = height_;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetRTVHandle() const
    {
        return rtvDescriptor_.Cpu();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffscreenRenderTarget::GetSRVHandle() const
    {
        return srvDescriptor_.Gpu();
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
}
