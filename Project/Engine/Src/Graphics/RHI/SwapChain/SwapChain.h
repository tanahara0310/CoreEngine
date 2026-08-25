#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"

namespace CoreEngine
{
    class DescriptorAllocator;

    /// @brief スワップチェーンの生成パラメータ
    struct SwapChainDesc {
        HWND hwnd = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bufferCount = 2;
        /// バックバッファのフォーマット。Flip モデルは _SRGB を受け付けないので非 sRGB で作り、
        /// sRGB 変換は RTV 側（rtvFormat）で掛ける
        DXGI_FORMAT bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        std::string debugName = "SwapChain";
    };

    /// @brief HWND 1 つ分のスワップチェーン（バックバッファ＋RTV＋ステート追跡）
    /// @details メインウィンドウも GameOutputWindow も同じクラスを使う
    /// @warning Resize() / Shutdown() は呼び出し側が WaitForGpuIdle してから呼ぶこと
    class SwapChain {
    public:
        SwapChain() = default;
        ~SwapChain();

        SwapChain(const SwapChain&) = delete;
        SwapChain& operator=(const SwapChain&) = delete;

        /// @brief 生成（スワップチェーン → バックバッファ取得 → RTV 作成）
        /// @param dxgiFactory         CreateSwapChainForHwnd を呼ぶファクトリ
        /// @param commandQueue        Present を載せるキュー（フレームと同じキューにすること）
        /// @param descriptorAllocator RTV スロットの確保先
        /// @return 失敗したら false（ログ済み）
        bool Initialize(IDXGIFactory7* dxgiFactory,
                        ID3D12CommandQueue* commandQueue,
                        DescriptorAllocator* descriptorAllocator,
                        const SwapChainDesc& desc);

        /// @brief 破棄（RTV を返し、バックバッファとスワップチェーンを手放す）
        void Shutdown();

        /// @brief バックバッファを作り直す（RTV は同じスロットへ書き直す）
        /// @return 失敗したら false（ログ済み）
        bool Resize(uint32_t width, uint32_t height);

        /// @brief 画面へ出す
        HRESULT Present(UINT syncInterval, UINT flags = 0);

        bool IsValid() const noexcept { return swapChain_ != nullptr; }
        IDXGISwapChain4* Get() const noexcept { return swapChain_.Get(); }

        /// @brief 今描くべきバックバッファの番号
        /// @warning per-frame リソースの添字に使わないこと（ResizeBuffers で 0 に戻る）
        uint32_t CurrentBackBufferIndex() const;

        uint32_t BufferCount() const noexcept { return static_cast<uint32_t>(backBuffers_.size()); }
        uint32_t Width() const noexcept { return desc_.width; }
        uint32_t Height() const noexcept { return desc_.height; }
        DXGI_FORMAT BufferFormat() const noexcept { return desc_.bufferFormat; }
        DXGI_FORMAT RTVFormat() const noexcept { return desc_.rtvFormat; }

        /// @brief バックバッファ（実体＋現在ステート）。バリアはこれを渡す
        GpuResource& BackBuffer(uint32_t index);
        /// @brief バックバッファの RTV
        D3D12_CPU_DESCRIPTOR_HANDLE RTV(uint32_t index) const;

    private:
        bool RetrieveBackBuffers();
        void ReleaseBackBuffers();
        void WriteRTVs();

        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
        std::vector<GpuResource> backBuffers_;
        std::vector<DescriptorHandle> rtvs_;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        SwapChainDesc desc_{};
    };
}
