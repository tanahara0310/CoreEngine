#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace CoreEngine
{
    // 前方宣言
    class WinApp;

    /// @brief DirectX12デバイスとDXGIファクトリの管理クラス
    class DeviceManager {
    public:
        /// @brief 初期化
        /// @param winApp ウィンドウアプリケーション
        /// @param enableDebugLayer デバッグレイヤーを有効にするか
        /// @param enableGPUBasedValidation GPU-Based Validationを有効にするか
        void Initialize(WinApp* winApp, bool enableDebugLayer, bool enableGPUBasedValidation);

        // アクセッサ
        ID3D12Device* GetDevice() const { return device_.Get(); }
        IDXGIFactory7* GetDXGIFactory() const { return dxgiFactory_.Get(); }

        /// @brief DXRレイトレーシングがサポートされているか
        bool IsDXRSupported() const { return isDXRSupported_; }

        /// @brief DXRレイトレーシングのティアを取得
        D3D12_RAYTRACING_TIER GetDXRTier() const { return dxrTier_; }

    private:
        /// @brief DXGIデバイスの初期化
        void InitializeDXGIDevice();

        /// @brief DXRサポートの確認
        void CheckDXRSupport();

    private:
        // DXGIファクトリとデバイス
        Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;

        // DXRサポート情報
        bool isDXRSupported_ = false;
        D3D12_RAYTRACING_TIER dxrTier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

        WinApp* winApp_ = nullptr;

        // デバッグ設定（コンフィグから取得）
        bool enableDebugLayer_ = false;
        bool enableGPUBasedValidation_ = false;
    };
}

