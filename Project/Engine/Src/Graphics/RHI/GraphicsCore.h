#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>
#include <vector>

#include "Graphics/RHI/Command/FrameSync.h" // FrameContext / kMaxFramesInFlight（値で返すため実体が要る）

/// @file
/// @brief DirectX12 基盤（RHI 層）のファサード

namespace CoreEngine
{
    class IResizable;
    struct GraphicsCoreDesc;

    class DeviceManager;
    class CommandQueue;
    class CommandContext;
    class DeferredReleaseQueue;
    class DescriptorAllocator;
    class SwapChain;
    class UploadContext;
    class UploadRing;

    /// @brief DirectX12 の基盤（デバイス・コマンド・ディスクリプタ・メインスワップチェーン）を束ねるファサード
    /// @note マネージャのヘッダを include しないこと（前方宣言のみ。アクセッサの実装は .cpp）
    class GraphicsCore {
    public:
        GraphicsCore();

        /// @brief デストラクタ（Shutdown が未呼び出しの場合も安全に GPU 待ちする）
        /// @note unique_ptr のメンバが不完全型なので、定義は必ず .cpp 側に置くこと
        ~GraphicsCore();

        GraphicsCore(const GraphicsCore&) = delete;
        GraphicsCore& operator=(const GraphicsCore&) = delete;

        /// @brief 初期化
        /// @param desc 出力先ウィンドウ・サイズ・デバッグ設定・フレーム数・ディスクリプタ数
        void Initialize(const GraphicsCoreDesc& desc);

        /// @brief シャットダウン処理（GPU完了待ちの後、全マネージャーを解放）
        void Shutdown();

        /// @brief ウィンドウリサイズ時の処理
        /// @details GPU を待ち、メインスワップチェーンを作り直し、登録済みの IResizable へ通知する
        void OnWindowResize(int32_t width, int32_t height);

        /// @brief ウィンドウリサイズ通知を受け取るオブジェクトを登録する
        /// @details 破棄時に UnregisterResizable() で解除すること
        void RegisterResizable(IResizable* resizable);

        /// @brief ウィンドウリサイズ通知の登録を解除する（未登録なら何もしない）
        void UnregisterResizable(IResizable* resizable);

        // ── デバイス ────────────────────────────────────────────
        ID3D12Device* GetDevice() const;
        IDXGIFactory7* GetDXGIFactory() const;

        // ── フレームのライフサイクル ────────────────────────────

        /// @brief フレーム開始。フレーム番号を進め、前フレームの後始末を回収する
        /// @return 今フレームの frameIndex / frameNumber / cmdList
        /// @note コマンドリストの Reset は EndFrame() の末尾で行う
        FrameContext BeginFrame();

        /// @brief フレーム終了。Close → Execute → Signal → Present → 次フレームの準備まで行う
        /// @param syncInterval Present の垂直同期間隔（1 = VSync 有効）
        void EndFrame(UINT syncInterval = 1);

        /// @brief フレーム同期の単一ソース
        /// @details per-frame リソースの添字は必ずここから取ること（CurrentBackBufferIndex() で代用しない）
        FrameSync& Frame() const;

        /// @brief GPU 完了後にリソースを解放する予約キュー
        /// @details その場で GPU を待つ代わりにここへ預けるとストールしない。
        DeferredReleaseQueue& DeferredRelease() const;

        // ── コマンド ────────────────────────────────────────────
        ID3D12CommandQueue* GetCommandQueue() const;
        ID3D12GraphicsCommandList* GetCommandList() const;

        /// @brief フレーム描画とは独立したアップロード／オフライン生成用コンテキストを取得
        /// @details テクスチャ・VB/IB のアップロードや IBL 生成は必ずこちらへ積むこと
        ///          （描画用リストへ積むとワーカースレッドの記録がフレーム記録と競合する）
        UploadContext* GetUploadContext() const;

        /// @brief 今フレームだけ有効な定数バッファ置き場
        /// @details 「毎フレーム内容が変わる小さな定数」はここから取ること
        ///          （フレーム数ぶんのスロットを持ち、GPU 完了済みのものだけを使い回す）
        UploadRing& GetUploadRing() const;

        /// @brief 投入済みの全 GPU 作業の完了を待つ（完全同期）
        /// @details リソースの作り直し・破棄の直前にだけ使うこと。
        void WaitForGpuIdle();

        // ── スワップチェーン ────────────────────────────────────
        /// @brief メインウィンドウのスワップチェーン（バックバッファ・RTV・Present）
        /// @details バックバッファは BackBuffer(i)、RTV は RTV(i)
        SwapChain& GetSwapChain() const;

        // ── ディスクリプタ ──────────────────────────────────────
        DescriptorAllocator* GetDescriptorAllocator() const;
        ID3D12DescriptorHeap* GetSRVHeap() const;

        // ── ウィンドウ ──────────────────────────────────────────
        /// @brief 現在のクライアント領域の大きさ（= メインスワップチェーンの大きさ）
        /// @details Initialize の desc で受け取り、OnWindowResize で更新する
        int32_t GetClientWidth() const noexcept { return clientWidth_; }
        int32_t GetClientHeight() const noexcept { return clientHeight_; }

    private:
        // 管理クラス（生成は .cpp のコンストラクタ。破棄順序は Shutdown で明示的に制御する）
        std::unique_ptr<DeviceManager> deviceManager_;
        std::unique_ptr<CommandQueue> commandQueue_;
        std::unique_ptr<FrameSync> frameSync_;
        std::unique_ptr<CommandContext> commandContext_;
        std::unique_ptr<DeferredReleaseQueue> deferredRelease_;
        std::unique_ptr<DescriptorAllocator> descriptorAllocator_;
        std::unique_ptr<SwapChain> swapChain_;
        std::unique_ptr<UploadContext> uploadContext_;
        std::unique_ptr<UploadRing> uploadRing_;

        std::vector<IResizable*> resizables_;

        int32_t clientWidth_ = 0;
        int32_t clientHeight_ = 0;
    };
}
