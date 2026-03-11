#pragma once
#include "RenderPass.h"
#include <d3d12.h>

namespace CoreEngine
{
    class RenderTarget;

    /// @brief バックバッファへの最終出力パス
    class BackBufferPass : public RenderPass {
    public:
        BackBufferPass() = default;
        ~BackBufferPass() override = default;

        const char* GetName() const override { return "BackBuffer"; }

        void Execute(const RenderContext& context) override;

        /// @brief 前のパスからの入力を設定
        void SetInput(const PassOutput& input) override {
            inputHandle_ = input.srvHandle;
        }

        /// @brief レンダーターゲットを設定
        /// @param target レンダーターゲット
        void SetRenderTarget(RenderTarget* target) {
            renderTarget_ = target;
        }

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE inputHandle_{};
        RenderTarget* renderTarget_ = nullptr;
    };
}
