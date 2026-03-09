#pragma once
#include "RenderPass.h"
#include <d3d12.h>

namespace CoreEngine
{
    /// @brief ポストエフェクト適用パス
    class PostEffectPass : public RenderPass {
    public:
        PostEffectPass() = default;
        ~PostEffectPass() override = default;

        const char* GetName() const override { return "PostEffect"; }

        void Execute(const RenderContext& context) override;

        /// @brief 出力されたSRVハンドルを取得
        /// @return ポストエフェクト適用後のSRVハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputHandle() const {
            return outputHandle_;
        }

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE outputHandle_{};
    };
}
