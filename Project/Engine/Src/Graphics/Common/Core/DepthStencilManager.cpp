#include "pch.h"
#include "DepthStencilManager.h"
#include "DescriptorManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"

#include <cassert>

using namespace Microsoft::WRL;

namespace CoreEngine
{
    namespace {
        Logger& logger = Logger::GetInstance();
    }

    // ---------------------------------------------------------------
    // DepthStencilManager
    // ---------------------------------------------------------------

    DepthStencilManager::~DepthStencilManager()
    {
        if (!descriptorManager_) {
            return;
        }
        if (dsvSlotIndex_ != UINT_MAX) {
            descriptorManager_->FreeDSVIndex(dsvSlotIndex_);
        }
        if (depthSRVSlotIndex_ != UINT_MAX) {
            descriptorManager_->FreeSRVIndex(depthSRVSlotIndex_);
        }
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
        logger.Infof(LogCategory::Graphics, LogSubCategory::RenderTarget,
            "深度ステンシルリソースをリサイズしました ({}x{}) - DSVは再利用\n", width, height);
#endif
    }

    void DepthStencilManager::BeginDepthWrite(ID3D12GraphicsCommandList* cmdList)
    {
        assert(cmdList);
        assert(isInitialized_ && "DepthStencilManager must be initialized before use");

        // DEPTH_WRITE 状態へ遷移（既に DEPTH_WRITE なら冗長バリアをスキップ）
        ResourceBarrierHelper::Transition(
            cmdList,
            depthStencilResource_.Get(),
            currentState_,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);

        // 深度バッファをクリア
        cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

#ifdef _DEBUG
        logger.Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Barrier,
            "[Depth] BeginDepthWrite: クリア完了");
#endif
    }

    void DepthStencilManager::CreateDepthStencilResource()
    {
        // Typeless フォーマットで作成することで DSV（D24_UNORM_S8）と SRV（R24_UNORM_X8）の両方を作成できる
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Width = width_;
        resourceDesc.Height = height_;
        resourceDesc.MipLevels = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        // クリア値の設定
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

        // リソースを作成
        depthStencilResource_ = ResourceFactory::CreateTextureResource(
            device_,
            resourceDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue);

#ifdef _DEBUG
        logger.Infof(LogCategory::Graphics, LogSubCategory::RenderTarget,
            "深度ステンシルリソースを作成しました ({}x{})\n", width_, height_);
#endif
    }

    void DepthStencilManager::CreateDepthStencilView()
    {
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
        dsvSlotIndex_ = descriptorManager_->GetDSVIndexFromCpuHandle(dsvHandle_);

        // 深度リソースの SRV を作成（初回のみ）
        CreateDepthShaderResourceView();
    }

    void DepthStencilManager::UpdateDepthStencilView()
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        // 既存のハンドルに直接DSVを更新
        device_->CreateDepthStencilView(
            depthStencilResource_.Get(),
            &dsvDesc,
            dsvHandle_);

        // SRV も既存の CPU ハンドルに更新
        if (depthSRVCpuHandle_.ptr != 0) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels       = 1;
            device_->CreateShaderResourceView(depthStencilResource_.Get(), &srvDesc, depthSRVCpuHandle_);
        }

#ifdef _DEBUG
        logger.Infof(LogCategory::Graphics, LogSubCategory::RenderTarget,
            "DSVを更新しました (既存ハンドルを再利用)\n");
#endif
    }

    void DepthStencilManager::CreateDepthShaderResourceView()
    {
        // R24G8_TYPELESS リソースから R24_UNORM_X8_TYPELESS として深度値を読み取る SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;

        descriptorManager_->CreateSRV(
            depthStencilResource_.Get(),
            srvDesc,
            depthSRVCpuHandle_,
            depthSRVGpuHandle_,
            "MainDepthStencilSRV"
        );
        depthSRVSlotIndex_ = descriptorManager_->GetSRVIndexFromCpuHandle(depthSRVCpuHandle_);
    }
}
