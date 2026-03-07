#include "OffScreenRenderTargetManager.h"
#include "DescriptorManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/Render.h"

#include <cassert>
#include <format>

using namespace Microsoft::WRL;

namespace CoreEngine
{
void OffScreenRenderTargetManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, std::int32_t width, std::int32_t height)
{
    device_ = device;
    descriptorManager_ = descriptorManager;
    CreateOffScreenRenderTarget(width, height);
    isInitialized_ = true;
}

void OffScreenRenderTargetManager::Resize(std::int32_t width, std::int32_t height)
{
    if (!isInitialized_ || width <= 0 || height <= 0) {
        return;
    }

    CreateOffScreenRenderTarget(width, height);
}

void OffScreenRenderTargetManager::CreateOffScreenRenderTarget(std::int32_t width, std::int32_t height)
{
    // リソース設定（共通）
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = static_cast<UINT64>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = texDesc.Format;
    // 統一されたクリアカラーを使用
    clearValue.Color[0] = Render::kClearColor[0];
    clearValue.Color[1] = Render::kClearColor[1];
    clearValue.Color[2] = Render::kClearColor[2];
    clearValue.Color[3] = Render::kClearColor[3];

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    // ===== 1枚目のオフスクリーンバッファ作成 =====
    HRESULT hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&offScreenResource_));
    assert(SUCCEEDED(hr));

    // ===== 2枚目のオフスクリーンバッファ作成 =====
    hr = device_->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&offScreen2Resource_));
    assert(SUCCEEDED(hr));

    // SRV設定（共通）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    // RTV設定（共通）
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    // ===== 1枚目のRTV・SRV作成 =====
#ifdef _DEBUG
    Logger::GetInstance().Log(
        std::format("オフスクリーンレンダーターゲット1を作成中...\n"),
        LogLevel::INFO, LogCategory::Graphics);
#endif

    if (!isInitialized_) {
        descriptorManager_->CreateRTV(
            offScreenResource_.Get(),
            rtvDesc,
            offscreenRtvHandle_,
            "OffScreenRenderTarget1"
        );

        descriptorManager_->CreateSRV(
            offScreenResource_.Get(),
            srvDesc,
            offscreenSrvCpuHandle_,
            offscreenSrvHandle_,
            "OffScreenRenderTarget1"
        );
    }

    // ===== 2枚目のRTV・SRV作成 =====
#ifdef _DEBUG
    Logger::GetInstance().Log(
        std::format("オフスクリーンレンダーターゲット2を作成中...\n"),
        LogLevel::INFO, LogCategory::Graphics);
#endif

    if (!isInitialized_) {
        descriptorManager_->CreateRTV(
            offScreen2Resource_.Get(),
            rtvDesc,
            offscreen2RtvHandle_,
            "OffScreenRenderTarget2"
        );

        descriptorManager_->CreateSRV(
            offScreen2Resource_.Get(),
            srvDesc,
            offscreen2SrvCpuHandle_,
            offscreen2SrvHandle_,
            "OffScreenRenderTarget2"
        );
    }

    if (isInitialized_) {
        UpdateViews();
    }
}

void OffScreenRenderTargetManager::UpdateViews()
{
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    device_->CreateRenderTargetView(offScreenResource_.Get(), &rtvDesc, offscreenRtvHandle_);
    device_->CreateShaderResourceView(offScreenResource_.Get(), &srvDesc, offscreenSrvCpuHandle_);

    device_->CreateRenderTargetView(offScreen2Resource_.Get(), &rtvDesc, offscreen2RtvHandle_);
    device_->CreateShaderResourceView(offScreen2Resource_.Get(), &srvDesc, offscreen2SrvCpuHandle_);
}
}
