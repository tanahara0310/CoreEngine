#pragma once
#include "RenderPass.h"
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief バックバッファへの最終出力パス
    class BackBufferPass : public RenderPass {
    public:
        BackBufferPass() = default;
        ~BackBufferPass() override = default;

        const char* GetName() const override { return "BackBuffer"; }

        void Execute(const RenderContext& context) override;

        /// @brief 入力テクスチャを設定
        /// @param inputHandle 入力SRVハンドル
        void SetInputTexture(D3D12_GPU_DESCRIPTOR_HANDLE inputHandle) {
            inputHandle_ = inputHandle;
        }

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE inputHandle_{};
    };
}
