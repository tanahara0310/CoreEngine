#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace CoreEngine
{
    /// @brief DirectX12デバイスとDXGIファクトリの管理クラス
    /// @note ウィンドウには依存しない（デバイス生成に HWND は不要）。
    class DeviceManager {
    public:
        /// @brief 初期化
        /// @param enableDebugLayer デバッグレイヤーを有効にするか
        /// @param enableGPUBasedValidation GPU-Based Validationを有効にするか
        /// @param enableDRED GPU クラッシュ時の詳細記録（DRED）を有効にするか
        /// @note いずれも **設定値のみ** で決まる（#ifdef _DEBUG では切り替えない）。
        ///       構成ごとの既定値は Engine/Config/config_*.json が持つ。
        void Initialize(bool enableDebugLayer, bool enableGPUBasedValidation, bool enableDRED);

        // アクセッサ
        ID3D12Device* GetDevice() const { return device_.Get(); }
        IDXGIFactory7* GetDXGIFactory() const { return dxgiFactory_.Get(); }

        /// @brief DXRレイトレーシングがサポートされているか
        bool IsDXRSupported() const { return isDXRSupported_; }

        /// @brief DXRレイトレーシングのティアを取得
        D3D12_RAYTRACING_TIER GetDXRTier() const { return dxrTier_; }

        /// @brief デバッグレイヤーが溜めたメッセージをログへ吸い出して空にする
        /// @return 吸い出した件数（デバッグレイヤー無効なら常に 0）
        /// @details ID3D12InfoQueue のメッセージは既定でデバッガの出力ウィンドウにしか出ず、
        ///          ログファイルには残らない。そのため「他人の環境でだけ落ちる」種類の
        ///          不具合（GPU 実行中リソースの解放など）が手元に届かなかった。
        ///          毎フレーム呼んでログへ落とすことで、再現環境のログだけで追えるようにする。
        size_t DrainDebugMessages();

    private:
        /// @brief DXGIデバイスの初期化
        void InitializeDXGIDevice();

        /// @brief DXRサポートの確認
        void CheckDXRSupport();

    private:
        // DXGIファクトリとデバイス
        Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;

        // デバッグレイヤーのメッセージ置き場。無効な構成では null のまま
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue_;

        // DXRサポート情報
        bool isDXRSupported_ = false;
        D3D12_RAYTRACING_TIER dxrTier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;

        // デバッグ設定（コンフィグから取得）
        bool enableDebugLayer_ = false;
        bool enableGPUBasedValidation_ = false;
        bool enableDRED_ = false;
    };
}

