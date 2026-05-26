#include "SwapChainManager.h"
#include "DescriptorManager.h"
#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <format>

using namespace Microsoft::WRL;

namespace CoreEngine
{
namespace{
    Logger& logger = Logger::GetInstance();
}

void SwapChainManager::Initialize(ID3D12Device* device, IDXGIFactory7* dxgiFactory,
    ID3D12CommandQueue* commandQueue, DescriptorManager* descriptorManager, CoreEngine::WinApp* winApp)
{
    device_ = device;
    dxgiFactory_ = dxgiFactory;
    commandQueue_ = commandQueue;
    descriptorManager_ = descriptorManager;
    winApp_ = winApp;

    logger.Log("SwapChainManager: 初期化開始\n", LogLevel::INFO, LogCategory::Graphics);

    CreateSwapChain();
    RetrieveBackBuffers();
    CreateRTVs();

    isInitialized_ = true;
    logger.Log("SwapChainManager: 初期化完了\n", LogLevel::INFO, LogCategory::Graphics);
}

void SwapChainManager::RetrieveBackBuffers()
{
    for (UINT i = 0; i < 2; ++i) {
        HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
        if (FAILED(hr)) {
            logger.Log(
                std::format("エラー: スワップチェーンバックバッファ[{}]の取得に失敗しました\n", i),
                LogLevel::Error, LogCategory::Graphics);
            throw std::runtime_error("Failed to get swap chain back buffer");
        }
        logger.Log(
            std::format("スワップチェーンバックバッファ[{}]取得完了: ptr={:#x}\n",
                i, reinterpret_cast<uintptr_t>(swapChainResources_[i].Get())),
            LogLevel::INFO, LogCategory::Graphics);
    }
}

void SwapChainManager::CreateRTVs()
{
    // RTVの設定
    rtvDesc_ = {};
    rtvDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc_.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    // DescriptorManager 経由でRTVヒープを取得し、予約スロット[0],[1]に直接書き込む
    // kReservedRTVStart(=0) から2スロットをスワップチェーン用として使用する
    ID3D12DescriptorHeap* rtvHeap = descriptorManager_->GetRTVHeap();
    const UINT rtvSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();

    rtvHandles_[0].ptr = rtvStart.ptr + DescriptorManager::kReservedRTVStart * rtvSize;
    rtvHandles_[1].ptr = rtvStart.ptr + (DescriptorManager::kReservedRTVStart + 1) * rtvSize;

    device_->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc_, rtvHandles_[0]);
    device_->CreateRenderTargetView(swapChainResources_[1].Get(), &rtvDesc_, rtvHandles_[1]);

    logger.Log(
        std::format("スワップチェーンRTV作成完了:\n"
                    "  RTV[{}] = バックバッファ0 (handle={:#x})\n"
                    "  RTV[{}] = バックバッファ1 (handle={:#x})\n"
                    "  フォーマット: DXGI_FORMAT_R8G8B8A8_UNORM_SRGB\n",
            DescriptorManager::kReservedRTVStart, rtvHandles_[0].ptr,
            DescriptorManager::kReservedRTVStart + 1, rtvHandles_[1].ptr),
        LogLevel::INFO, LogCategory::Graphics);
}

void SwapChainManager::Resize(std::int32_t width, std::int32_t height)
{
    logger.Log(
        std::format("SwapChainManager: リサイズ開始 ({}x{})\n", width, height),
        LogLevel::INFO, LogCategory::Graphics);

    // バックバッファのリソースを解放
    for (UINT i = 0; i < 2; ++i) {
        swapChainResources_[i].Reset();
        logger.Log(
            std::format("  バックバッファ[{}]解放完了\n", i),
            LogLevel::INFO, LogCategory::Graphics);
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
        logger.Log(
            std::format("エラー: スワップチェーンのリサイズに失敗しました! 幅={}, 高さ={}, HRESULT={:#010x}\n",
                width, height, static_cast<unsigned>(hr)),
            LogLevel::Error, LogCategory::Graphics);
        throw std::runtime_error("Failed to resize swap chain buffers!");
    }

    // バックバッファを再取得
    RetrieveBackBuffers();

    // RTVを再作成（予約スロット[0],[1]に上書き）
    CreateRTVs();

    logger.Log(
        std::format("SwapChainManager: リサイズ完了 ({}x{})\n", width, height),
        LogLevel::INFO, LogCategory::Graphics);
}

void SwapChainManager::CreateSwapChain()
{
    logger.Log(
        std::format("スワップチェーン作成開始: 解像度={}x{}, フォーマット=DXGI_FORMAT_R8G8B8A8_UNORM, バッファ数=2\n",
            winApp_->GetClientWidth(), winApp_->GetClientHeight()),
        LogLevel::INFO, LogCategory::Graphics);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};
    swapChainDesc_ = {};
    swapChainDesc_.Width = static_cast<UINT>(winApp_->GetClientWidth());
    swapChainDesc_.Height = static_cast<UINT>(winApp_->GetClientHeight());
    swapChainDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc_.SampleDesc.Count = 1;
    swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc_.BufferCount = 2;
    swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT result = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_,
        winApp_->GetHwnd(),
        &swapChainDesc_,
        nullptr, nullptr,
        &swapChain1);
    assert(SUCCEEDED(result) && "スワップチェーンの作成に失敗しました");

    result = swapChain1.As(&swapChain_);
    assert(SUCCEEDED(result) && "IDXGISwapChain4へのキャストに失敗しました");

    logger.Log("スワップチェーン作成完了\n", LogLevel::INFO, LogCategory::Graphics);
}
}
