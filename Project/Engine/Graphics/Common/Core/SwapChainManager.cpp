#include "SwapChainManager.h"
#include "Engine/WinApp/WinApp.h"
#include "Engine/Utility/Logger/Logger.h"

#include <cassert>
#include <format>

using namespace Microsoft::WRL;

namespace CoreEngine
{
namespace{
    Logger& logger = Logger::GetInstance();
}

void SwapChainManager::Initialize(ID3D12Device* device, IDXGIFactory7* dxgiFactory,
    ID3D12CommandQueue* commandQueue, ID3D12DescriptorHeap* rtvHeap, CoreEngine::WinApp* winApp)
{
    device_ = device;
    dxgiFactory_ = dxgiFactory;
    commandQueue_ = commandQueue;
    rtvHeap_ = rtvHeap;
    winApp_ = winApp;

    CreateSwapChain();
    RetrieveBackBuffers();
    CreateRTVs();
    
    isInitialized_ = true;
}

void SwapChainManager::RetrieveBackBuffers()
{
    for (UINT i = 0; i < 2; ++i) {
        swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
    }
}

void SwapChainManager::CreateRTVs()
{
    // RTVの設定
    rtvDesc_ = {};
    rtvDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 出力結果をSRGBに変換して書き込む
    rtvDesc_.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
    // ディスクプリタヒープの先頭を取得
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    // 1つ目を生成。1つ目は最初の所に作る。作る場所をこちらで指定する
    rtvHandles_[0] = rtvStartHandle;
    device_->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc_, rtvHandles_[0]);

    // 2つ目のディスクプリタハンドルを作る。(自力で)
    rtvHandles_[1].ptr = rtvHandles_[0].ptr + device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device_->CreateRenderTargetView(swapChainResources_[1].Get(), &rtvDesc_, rtvHandles_[1]);

#ifdef _DEBUG
    // 初回のみログ出力
    if (!isInitialized_) {
        logger.Log(
            std::format("RTV[0]を作成しました (スワップチェーン バックバッファ0用)\n"),
            LogLevel::INFO, LogCategory::Graphics);
        logger.Log(
            std::format("RTV[1]を作成しました (スワップチェーン バックバッファ1用)\n"),
            LogLevel::INFO, LogCategory::Graphics);
    }
#endif
}

void SwapChainManager::Resize(std::int32_t width, std::int32_t height)
{

    // バックバッファのリソースを解放
    for (UINT i = 0; i < 2; ++i) {
        swapChainResources_[i].Reset();
    }

    // スワップチェーンのバッファをリサイズ
    HRESULT hr = swapChain_->ResizeBuffers(
        2,  // バッファ数
        static_cast<UINT>(width),   // 新しい幅
        static_cast<UINT>(height),      // 新しい高さ
        DXGI_FORMAT_R8G8B8A8_UNORM,    // フォーマット
        0  // フラグ
    );

    if (FAILED(hr)) {
        logger.Log(
            std::format("エラー: スワップチェーンのリサイズに失敗しました! 幅={}, 高さ={}\n",
                width, height),
            LogLevel::Error, LogCategory::Graphics);
        throw std::runtime_error("Failed to resize swap chain buffers!");

    }

    // バックバッファを再取得
    RetrieveBackBuffers();

    // RTVを再作成（既存のハンドルRTV[0], RTV[1]を再利用）
    CreateRTVs();

#ifdef _DEBUG
    logger.Log(
        std::format("スワップチェーンRTVを更新しました ({}x{}) - ハンドルは再利用\n", width, height),
        LogLevel::INFO, LogCategory::Graphics);
#endif
}

void SwapChainManager::CreateSwapChain()
{
    HRESULT result = S_FALSE;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};
    swapChainDesc_ = {};
    swapChainDesc_.Width = WinApp::kClientWidth; // WinAppのスタティック定数から取得
    swapChainDesc_.Height = WinApp::kClientHeight; // WinAppのスタティック定数から取得
    swapChainDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
    swapChainDesc_.SampleDesc.Count = 1; // マルチサンプルしない
    swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットして利用する
    swapChainDesc_.BufferCount = 2; // ダブルバッファ
    swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタに映したら破棄
    // コマンドキュー、ウィンドウハンドル、設定を渡して生成
    ComPtr<IDXGISwapChain1> swapChain1;
    result = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_,
        winApp_->GetHwnd(),
        &swapChainDesc_,
        nullptr, nullptr,
        &swapChain1);
    // スワップチェーンの生成が上手く行かなかったので起動できない
    assert(SUCCEEDED(result));

    // swapChain1をswapChainにキャストし変換(これをやらないと例外エラー)
    result = swapChain1.As(&swapChain_);
    assert(SUCCEEDED(result));
}
}
