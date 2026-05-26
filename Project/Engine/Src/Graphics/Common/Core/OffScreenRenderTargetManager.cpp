#include "pch.h"
#include "OffScreenRenderTargetManager.h"
#include "DescriptorManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/Render.h"

#include <cassert>

using namespace Microsoft::WRL;

namespace CoreEngine
{

    OffScreenRenderTargetManager::~OffScreenRenderTargetManager()
    {
        // descriptorManager_ が有効な場合のみスロットを解放する
        if (!descriptorManager_) {
            return;
        }

        for (auto& target : offScreenTargets_) {
            if (target.rtvSlotIndex != UINT_MAX) {
                descriptorManager_->FreeRTVIndex(target.rtvSlotIndex);
            }
            if (target.srvSlotIndex != UINT_MAX) {
                descriptorManager_->FreeSRVIndex(target.srvSlotIndex);
            }
            if (target.uavSlotIndex != UINT_MAX) {
                descriptorManager_->FreeSRVIndex(target.uavSlotIndex);
            }
        }
    }

    void OffScreenRenderTargetManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, std::int32_t width, std::int32_t height, uint32_t initialTargetCount)
    {
        device_ = device;
        descriptorManager_ = descriptorManager;
        currentWidth_ = width;
        currentHeight_ = height;
        EnsureTargetCount(initialTargetCount);
        isInitialized_ = true;
    }

        void OffScreenRenderTargetManager::Resize(std::int32_t width, std::int32_t height)
    {
        if (!isInitialized_ || width <= 0 || height <= 0) {
            return;
        }

        currentWidth_ = width;
        currentHeight_ = height;

        for (uint32_t i = 0; i < offScreenTargets_.size(); ++i) {
            CreateOrResizeTargetResource(offScreenTargets_[i], i, width, height);
            UpdateTargetViews(offScreenTargets_[i]);
        }
    }

    void OffScreenRenderTargetManager::EnsureTargetCount(uint32_t count)
    {
        while (offScreenTargets_.size() < count) {
            const uint32_t index = static_cast<uint32_t>(offScreenTargets_.size());
            offScreenTargets_.emplace_back();
            auto& target = offScreenTargets_.back();

            CreateOrResizeTargetResource(target, index, currentWidth_, currentHeight_);
            CreateTargetViews(target, index);
        }
    }

    void OffScreenRenderTargetManager::CreateOrResizeTargetResource(OffScreenTarget& target, [[maybe_unused]] uint32_t index, std::int32_t width, std::int32_t height)
    {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = static_cast<UINT64>(width);
        texDesc.Height = static_cast<UINT>(height);
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = texDesc.Format;
        clearValue.Color[0] = target.clearColor[0];
        clearValue.Color[1] = target.clearColor[1];
        clearValue.Color[2] = target.clearColor[2];
        clearValue.Color[3] = target.clearColor[3];

        target.resource = ResourceFactory::CreateTextureResource(
            device_,
            texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue);

#ifdef _DEBUG
        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::RenderTarget,
            "オフスクリーンレンダーターゲット{}を作成中...\n", index);
#endif
    }

    void OffScreenRenderTargetManager::CreateTargetViews(OffScreenTarget& target, uint32_t index)
    {
        assert(target.resource);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        descriptorManager_->CreateRTV(
            target.resource.Get(),
            rtvDesc,
            target.rtvHandle,
            std::format("OffScreenRenderTarget{}", index)
        );
        target.rtvSlotIndex = descriptorManager_->GetRTVIndexFromCpuHandle(target.rtvHandle);

        descriptorManager_->CreateSRV(
            target.resource.Get(),
            srvDesc,
            target.srvCpuHandle,
            target.srvHandle,
            std::format("OffScreenRenderTarget{}", index)
        );
        target.srvSlotIndex = descriptorManager_->GetSRVIndexFromCpuHandle(target.srvCpuHandle);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        descriptorManager_->CreateUAV(
            target.resource.Get(),
            uavDesc,
            target.uavCpuHandle,
            target.uavHandle,
            std::format("OffScreenRenderTarget{}UAV", index)
        );
        target.uavSlotIndex = descriptorManager_->GetSRVIndexFromCpuHandle(target.uavCpuHandle);
    }

    void OffScreenRenderTargetManager::UpdateTargetViews(const OffScreenTarget& target)
    {
        assert(target.resource);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        device_->CreateRenderTargetView(target.resource.Get(), &rtvDesc, target.rtvHandle);
        device_->CreateShaderResourceView(target.resource.Get(), &srvDesc, target.srvCpuHandle);
    }

    void OffScreenRenderTargetManager::ValidateIndex([[maybe_unused]] uint32_t index) const
    {
        assert(index < offScreenTargets_.size());
    }

    ID3D12Resource* OffScreenRenderTargetManager::GetOffScreenResource(uint32_t index) const
    {
        ValidateIndex(index);
        return offScreenTargets_[index].resource.Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE OffScreenRenderTargetManager::GetOffScreenRtvHandle(uint32_t index) const
    {
        ValidateIndex(index);
        return offScreenTargets_[index].rtvHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffScreenRenderTargetManager::GetOffScreenSrvHandle(uint32_t index) const
    {
        ValidateIndex(index);
        return offScreenTargets_[index].srvHandle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE OffScreenRenderTargetManager::GetOffScreenUavHandle(uint32_t index) const
    {
        ValidateIndex(index);
        return offScreenTargets_[index].uavHandle;
    }

    D3D12_RESOURCE_STATES OffScreenRenderTargetManager::GetOffScreenState(uint32_t index) const
    {
        ValidateIndex(index);
        return offScreenTargets_[index].currentState;
    }

    void OffScreenRenderTargetManager::SetOffScreenState(uint32_t index, D3D12_RESOURCE_STATES state)
    {
        ValidateIndex(index);
        offScreenTargets_[index].currentState = state;
    }

    void OffScreenRenderTargetManager::SetTargetClearColor(uint32_t index, const float color[4])
    {
        ValidateIndex(index);
        auto& target = offScreenTargets_[index];
        target.clearColor[0] = color[0];
        target.clearColor[1] = color[1];
        target.clearColor[2] = color[2];
        target.clearColor[3] = color[3];
        // リソースを再作成してクリア色を反映
        CreateOrResizeTargetResource(target, index, currentWidth_, currentHeight_);
        UpdateTargetViews(target);
    }
}


