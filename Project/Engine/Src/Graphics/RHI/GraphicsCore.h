#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <memory>
#include <vector>

/// @file
/// @brief DirectX12 基盤（RHI 層）のファサード

namespace CoreEngine
{
    class WinApp;
    class IResizable;
    struct EngineConfig;

    class DeviceManager;
    class CommandManager;
    class DescriptorManager;
    class SwapChainManager;
    class DepthStencilManager;
    class UploadContext;

    /// @brief DirectX12 の基盤（デバイス・コマンド・スワップチェーン・各種マネージャ）を束ねるファサード
    ///
    /// @details
    /// ここは **ハブであって実装ではない**。所有・初期化順序・破棄順序だけを持ち、
    /// 実際の仕事は各マネージャが行う。
    ///
    /// @note このヘッダは約 100 ファイルから include されるため、
    ///       **マネージャのヘッダを include してはならない**（前方宣言のみ）。
    ///       アクセッサの実装が .cpp にあるのはそのため。
    ///       マネージャの型が必要な呼び出し側は、自分で該当ヘッダを include すること。
    class GraphicsCore {
    public:
        GraphicsCore();

        /// @brief デストラクタ（Shutdown が未呼び出しの場合も安全に GPU 待ちする）
        /// @note unique_ptr のメンバが不完全型なので、定義は必ず .cpp 側に置くこと
        ~GraphicsCore();

        GraphicsCore(const GraphicsCore&) = delete;
        GraphicsCore& operator=(const GraphicsCore&) = delete;

        /// @brief 初期化
        /// @param winApp ウィンドウアプリケーション
        /// @param config エンジン設定
        void Initialize(WinApp* winApp, const EngineConfig& config);

        /// @brief シャットダウン処理（GPU完了待ちの後、全マネージャーを解放）
        void Shutdown();

        /// @brief ウィンドウリサイズ時の処理
        void OnWindowResize(int32_t width, int32_t height);

        /// @brief ウィンドウリサイズ通知を受け取るクラスを登録する
        /// @details スワップチェーン / 深度バッファの再作成後に OnWindowResize() が呼ばれる。
        /// @warning 登録したオブジェクトを GraphicsCore より先に破棄してはならない
        void RegisterResizable(IResizable* resizable);

        // ── デバイス ────────────────────────────────────────────
        ID3D12Device* GetDevice() const;
        IDXGIFactory7* GetDXGIFactory() const;

        // ── コマンド ────────────────────────────────────────────
        ID3D12CommandQueue* GetCommandQueue() const;
        ID3D12GraphicsCommandList* GetCommandList() const;
        CommandManager* GetCommandManager() const;

        /// @brief フレーム描画とは独立したアップロード／オフライン生成用コンテキストを取得
        /// @details テクスチャ・VB/IB のアップロードや IBL 生成は必ずこちらへ積むこと。
        ///          GetCommandList()（＝フレームの描画用リスト）へ積むと、ワーカースレッドからの
        ///          記録がメインスレッドの描画記録と競合し、フレーム外の Close/Execute が
        ///          記録途中のフレームを巻き添えで submit する。
        UploadContext* GetUploadContext() const;

        /// @brief 投入済みの全 GPU 作業の完了を待つ（完全同期）
        /// @details リソースの作り直し・破棄の直前にだけ使うこと。
        void WaitForGpuIdle();

        // ── スワップチェーン ────────────────────────────────────
        IDXGISwapChain4* GetSwapChain() const;
        ID3D12Resource* GetSwapChainBackBuffer(UINT index) const;
        D3D12_RENDER_TARGET_VIEW_DESC GetRTVDesc() const;
        const D3D12_CPU_DESCRIPTOR_HANDLE& GetRTVHandle(UINT index) const;

        // ── ディスクリプタ ──────────────────────────────────────
        DescriptorManager* GetDescriptorManager() const;
        ID3D12DescriptorHeap* GetSRVHeap() const;
        ID3D12DescriptorHeap* GetDSVHeap() const;

        // ── 深度ステンシル ──────────────────────────────────────
        DepthStencilManager* GetDepthStencilManager() const;
        ID3D12Resource* GetDepthStencilResource() const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const;
        /// @brief 深度テクスチャの SRV GPU ハンドルを返す（水面 Depth Fade 等で使用）
        D3D12_GPU_DESCRIPTOR_HANDLE GetDepthStencilSRV() const;

        // ── ウィンドウ ──────────────────────────────────────────
        int32_t GetClientWidth() const;
        int32_t GetClientHeight() const;

    private:
        // ウィンドウズアプリケーション管理
        WinApp* winApp_ = nullptr;

        // 管理クラス（生成は .cpp のコンストラクタ。破棄順序は Shutdown で明示的に制御する）
        std::unique_ptr<DeviceManager> deviceManager_;
        std::unique_ptr<CommandManager> commandManager_;
        std::unique_ptr<DescriptorManager> descriptorManager_;
        std::unique_ptr<SwapChainManager> swapChainManager_;
        std::unique_ptr<DepthStencilManager> depthStencilManager_;
        std::unique_ptr<UploadContext> uploadContext_;

        std::vector<IResizable*> resizables_;
    };
}
