#include "pch.h"
#include "Graphics/RHI/SwapChain/SwapChainManager.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Utility/Logger/Logger.h"

#include <cassert>

using namespace Microsoft::WRL;

namespace CoreEngine
{
namespace{
    Logger& logger = Logger::GetInstance();
}

void SwapChainManager::Initialize(ID3D12Device* device, IDXGIFactory7* dxgiFactory,
    ID3D12CommandQueue* commandQueue, DescriptorAllocator* descriptorAllocator,
    HWND hwnd, std::int32_t width, std::int32_t height)
{
    device_ = device;
    dxgiFactory_ = dxgiFactory;
    commandQueue_ = commandQueue;
    descriptorAllocator_ = descriptorAllocator;
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;

    logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain, "SwapChainManager: 初期化開始\n");

    CreateSwapChain();
    RetrieveBackBuffers();
    CreateRTVs();

    isInitialized_ = true;
    logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain, "SwapChainManager: 初期化完了\n");
}

void SwapChainManager::RetrieveBackBuffers()
{
    for (UINT i = 0; i < 2; ++i) {
        HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
        if (FAILED(hr)) {
            logger.Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "エラー: スワップチェーンバックバッファ[{}]の取得に失敗しました\n", i);
            throw std::runtime_error("Failed to get swap chain back buffer");
        }
        logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
            "スワップチェーンバックバッファ[{}]取得完了: ptr={:#x}\n",
            i, reinterpret_cast<uintptr_t>(swapChainResources_[i].Get()));
    }
}

void SwapChainManager::CreateRTVs()
{
    // RTVの設定
    rtvDesc_ = {};
    rtvDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc_.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    // 旧実装はヒープ先頭から手計算で「予約スロット[0][1]」へ直接書き込んでいた
    // （DescriptorAllocator を素通りするため、確保状況にも載らなかった）。
    // 今は普通に確保する。ハンドルがスロット番号を持つので予約という概念自体が要らない。
    for (UINT i = 0; i < 2; ++i) {
        if (rtvDescriptors_[i].IsValid()) {
            // リサイズ経路：同じスロットへ書き直す（RTV の位置を変えない）
            descriptorAllocator_->WriteRTV(rtvDescriptors_[i], swapChainResources_[i].Get(), rtvDesc_);
        } else {
            rtvDescriptors_[i] = descriptorAllocator_->CreateRTV(
                swapChainResources_[i].Get(), rtvDesc_,
                i == 0 ? "SwapChainBackBuffer0" : "SwapChainBackBuffer1");
        }
    }

    logger.Log(
        std::format("スワップチェーンRTV作成完了:\n"
                    "  RTV[{}] = バックバッファ0 (handle={:#x})\n"
                    "  RTV[{}] = バックバッファ1 (handle={:#x})\n"
                    "  フォーマット: DXGI_FORMAT_R8G8B8A8_UNORM_SRGB\n",
            rtvDescriptors_[0].index, rtvDescriptors_[0].cpuHandle.ptr,
            rtvDescriptors_[1].index, rtvDescriptors_[1].cpuHandle.ptr),
        LogLevel::INFO, LogCategory::Graphics);
}

void SwapChainManager::Resize(std::int32_t width, std::int32_t height)
{
    logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
        "SwapChainManager: リサイズ開始 ({}x{})\n", width, height);

    // バックバッファのリソースを解放
    for (UINT i = 0; i < 2; ++i) {
        swapChainResources_[i].Reset();
        logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
            "  バックバッファ[{}]解放完了\n", i);
    }

    // スワップチェーンのバッファをリサイズ
    HRESULT hr = swapChain_->ResizeBuffers(
        2,
        static_cast<UINT>(width),
        static_cast<UINT>(height),
        DXGI_FORMAT_R8G8B8A8_UNORM,
        0
    );

    if (FAILED(hr)) {
        logger.Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
            "エラー: スワップチェーンのリサイズに失敗しました! 幅={}, 高さ={}, HRESULT={:#010x}\n",
            width, height, static_cast<unsigned>(hr));
        throw std::runtime_error("Failed to resize swap chain buffers!");
    }

    // バックバッファを再取得
    RetrieveBackBuffers();

    // RTVを再作成（予約スロット[0],[1]に上書き）
    CreateRTVs();

    logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
        "SwapChainManager: リサイズ完了 ({}x{})\n", width, height);
}

void SwapChainManager::CreateSwapChain()
{
    logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
        "スワップチェーン作成開始: 解像度={}x{}, フォーマット=DXGI_FORMAT_R8G8B8A8_UNORM, バッファ数=2\n",
        width_, height_);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};
    swapChainDesc_ = {};
    swapChainDesc_.Width = static_cast<UINT>(width_);
    swapChainDesc_.Height = static_cast<UINT>(height_);
    swapChainDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc_.SampleDesc.Count = 1;
    swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc_.BufferCount = 2;
    swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT result = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_,
        hwnd_,
        &swapChainDesc_,
        nullptr, nullptr,
        &swapChain1);
    assert(SUCCEEDED(result) && "スワップチェーンの作成に失敗しました");

    result = swapChain1.As(&swapChain_);
    assert(SUCCEEDED(result) && "IDXGISwapChain4へのキャストに失敗しました");

    logger.Infof(LogCategory::Graphics, LogSubCategory::SwapChain, "スワップチェーン作成完了\n");
}
}
