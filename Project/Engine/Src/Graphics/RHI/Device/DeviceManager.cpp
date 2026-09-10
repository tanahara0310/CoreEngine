#include "pch.h"
#include "Graphics/RHI/Device/DeviceManager.h"
#include "Graphics/RHI/Device/DeviceRemovedHandler.h"
#include "Utility/Logger/Logger.h"

#include <iostream>
#include <cassert>
#include <cstdint>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

namespace CoreEngine
{
void DeviceManager::Initialize(bool enableDebugLayer, bool enableGPUBasedValidation, bool enableDRED)
{
    enableDebugLayer_ = enableDebugLayer;
    enableGPUBasedValidation_ = enableGPUBasedValidation;
    enableDRED_ = enableDRED;
    InitializeDXGIDevice();
}

void DeviceManager::InitializeDXGIDevice()
{
    Logger& logger = Logger::GetInstance();

    // デバッグレイヤーの有効化（構成ではなくコンフィグの設定値だけで決まる）
    if (enableDebugLayer_) {
        ComPtr<ID3D12Debug1> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
            // デバッグレイヤーを有効にする
            debugController->EnableDebugLayer();
            // GPU-Based Validation（コンフィグで有効にした場合のみ）
            // 非常に重い（10〜100倍の速度低下）ため、必要な場合のみ有効化する
            debugController->SetEnableGPUBasedValidation(enableGPUBasedValidation_ ? TRUE : FALSE);

            // デバッグレイヤー有効化のログを出力
            OutputDebugString(L"Direct3D 12 デバッグレイヤーが有効化されました。\n");
            std::cout << "Direct3D 12 デバッグレイヤーが有効化されました。" << std::endl;
            if (enableGPUBasedValidation_) {
                OutputDebugString(L"GPU-Based Validation が有効化されました。\n");
                std::cout << "GPU-Based Validation が有効化されました。" << std::endl;
            }
        } else {
            OutputDebugString(L"Direct3D 12 デバッグインターフェースの取得に失敗しました。\n");
            std::cerr << "Direct3D 12 デバッグインターフェースの取得に失敗しました。" << std::endl;
            // 標準出力にしか出ないと、他人の環境のログを見たときに「エラーが無い」のか
            // 「そもそも検査できていない」のかが区別できない
            logger.Warnf(LogCategory::Graphics, LogSubCategory::Device,
                "デバッグレイヤーを有効にできませんでした。Windows の「グラフィックス ツール」"
                "オプション機能が未インストールの可能性があります");
        }
    }

    // DRED はデバイス生成より前でなければ効かないので、ここで有効化する
    if (enableDRED_) {
        EnableDeviceRemovedExtendedData();
    }

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

    // DXRサポートの確認
    CheckDXRSupport();

    // 情報キューはデバッグレイヤーが有効なときだけ取得できる（無効なら QueryInterface が失敗する）
    if (enableDebugLayer_) {
        ComPtr<ID3D12InfoQueue>& infoQueue = infoQueue_;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {

            // ブレークはデバッガが居るときだけ有効にする。
            // SetBreakOnSeverity は DebugBreak() を呼ぶので、デバッガが居ない環境では
            // 未処理のブレークポイント例外になり「最初の警告が出た瞬間にプロセスが
            // 黙って落ちる」。そのせいで、デバッグレイヤーを有効にしたまま他人へ
            // 渡して再現ログを採る、という一番やりたいことができなかった。
            // デバッガが居なければ止めずにログへ流す（DrainDebugMessages）。
            const BOOL breakOnMessage = ::IsDebuggerPresent();
            // ヤバいエラー時に止まる
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, breakOnMessage);
            // エラー時に止まる
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, breakOnMessage);
            // 警告時に止まる(コメントアウトすることで解放漏れが詳細にわかる)
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, breakOnMessage);

            // 抑制するメッセージのID
            D3D12_MESSAGE_ID denyIds[] = {
                // Windows11でのDXGIデバッグレイヤーとのDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
                // https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
                // DXR 加速構造バッファ作成時の InitialState 警告を抑制（ドライバが暗黙昇格する）
                D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED
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

            logger.Infof(LogCategory::Graphics, LogSubCategory::Device,
                "デバッグレイヤーのメッセージをログへ記録します（[D3D12 ...] の行が該当）");
        } else {
            logger.Warnf(LogCategory::Graphics, LogSubCategory::Device,
                "InfoQueue を取得できませんでした。デバッグレイヤーのメッセージはログに残りません");
        }
    }
}

size_t DeviceManager::DrainDebugMessages()
{
    if (!infoQueue_) {
        return 0; // デバッグレイヤーが無効な構成。ここが毎フレーム通る道なので即返す
    }

    const UINT64 count = infoQueue_->GetNumStoredMessages();
    if (count == 0) {
        return 0;
    }

    Logger& logger = Logger::GetInstance();
    std::vector<uint8_t> buffer;

    for (UINT64 i = 0; i < count; ++i) {
        // 1 回目の呼び出しで必要な大きさを聞き、詰め直してから本体を受け取る
        SIZE_T length = 0;
        if (FAILED(infoQueue_->GetMessage(i, nullptr, &length)) || length == 0) {
            continue;
        }
        buffer.resize(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (FAILED(infoQueue_->GetMessage(i, message, &length))) {
            continue;
        }

        // pDescription は NUL 終端済み。DescriptionByteLength は終端を含む長さ
        const char* text = message->pDescription ? message->pDescription : "(説明なし)";
        const int id = static_cast<int>(message->ID);

        switch (message->Severity) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "[D3D12 CORRUPTION] #{} {}", id, text);
            break;
        case D3D12_MESSAGE_SEVERITY_ERROR:
            logger.Errorf(LogCategory::Graphics, LogSubCategory::Device,
                "[D3D12 ERROR] #{} {}", id, text);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            logger.Warnf(LogCategory::Graphics, LogSubCategory::Device,
                "[D3D12 WARNING] #{} {}", id, text);
            break;
        default:
            logger.Infof(LogCategory::Graphics, LogSubCategory::Device,
                "[D3D12 INFO] #{} {}", id, text);
            break;
        }
    }

    infoQueue_->ClearStoredMessages();
    return static_cast<size_t>(count);
}

void DeviceManager::CheckDXRSupport()
{
    Logger& logger = Logger::GetInstance();

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    HRESULT hr = device_->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5));

    if (SUCCEEDED(hr) && options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        isDXRSupported_ = true;
        dxrTier_ = options5.RaytracingTier;

        const char* tierStr = "Unknown";
        switch (dxrTier_) {
        case D3D12_RAYTRACING_TIER_1_0: tierStr = "1.0"; break;
        case D3D12_RAYTRACING_TIER_1_1: tierStr = "1.1"; break;
        default: break;
        }
        logger.Logf(LogLevel::Info, LogCategory::Graphics,
            "DXR Raytracing supported (Tier {})", tierStr);
    } else {
        isDXRSupported_ = false;
        dxrTier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        logger.Log("DXR Raytracing not supported on this device",
            LogLevel::Warn, LogCategory::Graphics);
    }
}
}
