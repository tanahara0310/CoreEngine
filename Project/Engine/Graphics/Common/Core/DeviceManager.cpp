#include "DeviceManager.h"
#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"

#include <iostream>
#include <cassert>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

namespace CoreEngine
{
void DeviceManager::Initialize(WinApp* winApp)
{
    winApp_ = winApp;
    InitializeDXGIDevice();
}

void DeviceManager::InitializeDXGIDevice()
{
    Logger& logger = Logger::GetInstance();

    // デバッグレイヤーの有効化
#ifdef _DEBUG
    ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
        // デバッグレイヤーを有効にする
        debugController->EnableDebugLayer();
        // さらにGPUでチェックを行うようにする
        debugController->SetEnableGPUBasedValidation(TRUE);

        // デバッグレイヤー有効化のログを出力
        OutputDebugString(L"Direct3D 12 デバッグレイヤーが有効化されました。\n");
        std::cout << "Direct3D 12 デバッグレイヤーが有効化されました。" << std::endl;
    } else {
        OutputDebugString(L"Direct3D 12 デバッグインターフェースの取得に失敗しました。\n");
        std::cerr << "Direct3D 12 デバッグインターフェースの取得に失敗しました。" << std::endl;
    }
#endif

    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
    assert(SUCCEEDED(hr));

    // 使用するアダプタ用の変数。最初にnullptrを入れる
    ComPtr<IDXGIAdapter4> useAdapter = nullptr;
    // 良い順にアダプタを取得する
    for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(
                         i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter))
        != DXGI_ERROR_NOT_FOUND;
        ++i) {
        // アダプタの情報を取得
        DXGI_ADAPTER_DESC3 adapterDesc {};
        hr = useAdapter->GetDesc3(&adapterDesc);
        // アダプタを取得できなければ落とす
        assert(SUCCEEDED(hr));
        // ソフトウェアアダプタで無ければ採用
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            logger.Log(std::format(L"Use Adapater:{}", adapterDesc.Description), LogLevel::INFO, LogCategory::System);
            break;
        }

        // ソフトウェアアダプタだったら解放
        useAdapter = nullptr;
    }

    // 適切なアダプタが取得できなかったら終了
    assert(useAdapter != nullptr);

    // 機能レベルログ出力用の文字列
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };
    const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
    // 高い順に生成出来るか試す
    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        // 採用したアダプターでデバイスを生成
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
        // 指定した機能レベルでデバイスが生成できたかを確認
        if (SUCCEEDED(hr)) {
            // 生成出来たのでログ出力を行ってリープを抜ける
            logger.Log(std::format("Feature Level:{}", featureLevelStrings[i]), LogLevel::INFO, LogCategory::System);
            break;
        }
    }

    // デバイスの生成が上手く行かなかったので起動できない
    assert(device_ != nullptr);
    // 初期化完了のログ出力
    logger.Log("Complete create D3D12Device!!!", LogLevel::INFO, LogCategory::System);

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {

        // ヤバいエラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        // エラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        // 警告時に止まる(コメントアウトすることで解放漏れが詳細にわかる)
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        // 抑制するメッセージのID
        D3D12_MESSAGE_ID denyIds[] = {
            // Windows11でのDXGIデバッグレイヤーとのDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
            // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
            D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
        };

        // 抑制するレベル
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
        D3D12_INFO_QUEUE_FILTER filter {};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        // 指定したメッセージの表示を抑制する
        infoQueue->PushStorageFilter(&filter);
    }
#endif
}
}
