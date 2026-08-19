#include "pch.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"

#include <array>
#include <cassert>
#include <format>

#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

namespace
{
    using CoreEngine::GBufferManager;

    constexpr std::array<DXGI_FORMAT, GBufferManager::kTargetCount> kGBufferFormats = {
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,    // AlbedoAO
        DXGI_FORMAT_R16G16B16A16_FLOAT,      // NormalRoughness
        DXGI_FORMAT_R8G8B8A8_UNORM,          // EmissiveMetallic
        DXGI_FORMAT_R16G16_FLOAT,            // MotionVector
    };

    constexpr std::array<std::array<float, 4>, GBufferManager::kTargetCount> kGBufferClearColors = {
        std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f },  // AlbedoAO
        std::array<float, 4>{ 0.0f, 0.0f, 1.0f, 1.0f },  // NormalRoughness
        std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f },  // EmissiveMetallic
        std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f },  // MotionVector
    };

        /// @brief ターゲット種別を PIX / デバッグレイヤー用の名前へ変換する
    const char* ToDebugName(GBufferManager::Target target)
    {
        switch (target) {
        case GBufferManager::Target::AlbedoAO:       return "GBuffer_AlbedoAO";
        case GBufferManager::Target::NormalRoughness:  return "GBuffer_NormalRoughness";
        case GBufferManager::Target::EmissiveMetallic: return "GBuffer_EmissiveMetallic";
        case GBufferManager::Target::MotionVector:      return "GBuffer_MotionVector";
        default:
            return "GBuffer_Unknown";
        }
    }
}

namespace CoreEngine
{
    // G-Buffer 一式を作る。フォーマットは kGBufferFormats 固定で、実行時には変えない
    void GBufferManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, int32_t width, int32_t height)
    {
        device_ = device;
        descriptorManager_ = descriptorManager;
        currentWidth_ = width;
        currentHeight_ = height;

        ValidateState();

        for (uint32_t i = 0; i < kTargetCount; ++i) {
            CreateOrResizeTarget(static_cast<Target>(i));
        }

        isInitialized_ = true;

#ifdef _DEBUG
        Logger::GetInstance().Logf(
            LogLevel::INFO,
            LogCategory::Graphics,
            "GBufferManager initialized ({}x{}, {} targets). Legacy OffScreenRenderTarget geometry path remains active until GBufferPass migration is complete.",
            currentWidth_,
            currentHeight_,
            kTargetCount);
#endif
    }

    // 画面サイズが変わったら全ターゲットを作り直す（RTV / SRV も張り直す）
    void GBufferManager::Resize(int32_t width, int32_t height)
    {
        if (!isInitialized_ || width <= 0 || height <= 0) {
            return;
        }

        currentWidth_ = width;
        currentHeight_ = height;

        for (uint32_t i = 0; i < kTargetCount; ++i) {
            CreateOrResizeTarget(static_cast<Target>(i));
        }
    }

    void GBufferManager::BeginGeometryPass(
        ID3D12GraphicsCommandList* cmdList,
        DepthStencilManager* depthStencilManager,
        ID3D12DescriptorHeap* srvHeap)
    {
        // 全 RT を一括で OMSetRenderTargets する。深度は共有 DSV を使う
        assert(cmdList);
        assert(depthStencilManager);
        ValidateState();

        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kTargetCount> rtvHandles{};

        for (uint32_t i = 0; i < kTargetCount; ++i) {
            auto& target = targets_[i];
            rtvHandles[i] = target.rtvHandle;
            cmdList->ClearRenderTargetView(target.rtvHandle, kGBufferClearColors[i].data(), 0, nullptr);
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = depthStencilManager->GetDSVHandle();
        cmdList->OMSetRenderTargets(kTargetCount, rtvHandles.data(), FALSE, &dsvHandle);

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(currentWidth_);
        viewport.Height = static_cast<float>(currentHeight_);
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &viewport);

        D3D12_RECT scissor{};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = currentWidth_;
        scissor.bottom = currentHeight_;
        cmdList->RSSetScissorRects(1, &scissor);

        if (srvHeap) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            cmdList->SetDescriptorHeaps(1, heaps);
        }
    }

    ID3D12Resource* GBufferManager::GetResource(Target target) const
    {
        ValidateState();
        return targets_[ToIndex(target)].resource.Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GBufferManager::GetRTVHandle(Target target) const
    {
        ValidateState();
        return targets_[ToIndex(target)].rtvHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GBufferManager::GetSRVHandle(Target target) const
    {
        ValidateState();
        return targets_[ToIndex(target)].srvHandle;
    }

    DXGI_FORMAT GBufferManager::GetFormat(Target target) const
    {
        return kGBufferFormats[ToIndex(target)];
    }

    const DXGI_FORMAT* GBufferManager::GetFormats() const
    {
        return kGBufferFormats.data();
    }

    D3D12_RESOURCE_STATES& GBufferManager::GetCurrentState(Target target)
    {
        ValidateState();
        return targets_[ToIndex(target)].currentState;
    }

    void GBufferManager::CreateOrResizeTarget(Target target)
    {
        // フォーマットはターゲットごとに固定（kGBufferFormats）。
        // 既存があれば作り直しになるので、RTV / SRV も同じスロットへ張り直す
        ValidateState();

        const uint32_t index = ToIndex(target);
        auto& targetResource = targets_[index];

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = static_cast<UINT64>(currentWidth_);
        texDesc.Height = static_cast<UINT>(currentHeight_);
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = kGBufferFormats[index];
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = texDesc.Format;
        for (size_t i = 0; i < 4; ++i) {
            clearValue.Color[i] = kGBufferClearColors[index][i];
        }

        targetResource.resource = ResourceFactory::CreateTextureResource(
            device_,
            texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue);

        targetResource.currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        CreateViews(targetResource, target, targetResource.rtvHandle.ptr == 0);

#ifdef _DEBUG
        Logger::GetInstance().Logf(
            LogLevel::INFO,
            LogCategory::Graphics,
            "{} recreated ({}x{}, format={})",
            ToDebugName(target),
            currentWidth_,
            currentHeight_,
            static_cast<int>(texDesc.Format));
#endif
    }

    void GBufferManager::CreateViews(TargetResource& targetResource, Target target, bool createNewDescriptors)
    {
        assert(targetResource.resource);

        const uint32_t index = ToIndex(target);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = kGBufferFormats[index];
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = kGBufferFormats[index];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (createNewDescriptors) {
            descriptorManager_->CreateRTV(
                targetResource.resource.Get(),
                rtvDesc,
                targetResource.rtvHandle,
                ToDebugName(target));

            descriptorManager_->CreateSRV(
                targetResource.resource.Get(),
                srvDesc,
                targetResource.srvCpuHandle,
                targetResource.srvHandle,
                ToDebugName(target));
            return;
        }

        device_->CreateRenderTargetView(targetResource.resource.Get(), &rtvDesc, targetResource.rtvHandle);
        device_->CreateShaderResourceView(targetResource.resource.Get(), &srvDesc, targetResource.srvCpuHandle);
    }

    void GBufferManager::ValidateState() const
    {
        assert(device_ != nullptr);
        assert(descriptorManager_ != nullptr);
        assert(currentWidth_ > 0);
        assert(currentHeight_ > 0);
    }

    uint32_t GBufferManager::ToIndex(Target target) const
    {
        const uint32_t index = static_cast<uint32_t>(target);
        assert(index < kTargetCount);
        return index;
    }
}
