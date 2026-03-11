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

        /// @brief 前のパスからの入力を設定
        void SetInput(const PassOutput& input) override {
            inputHandle_ = input.srvHandle;
        }

        /// @brief このパスの出力を取得
        PassOutput GetOutput() const override { return output_; }

        /// @brief 出力されたSRVハンドルを取得（後方互換性のため残す）
        /// @return ポストエフェクト適用後のSRVハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetOutputHandle() const {
            return output_.srvHandle;
        }

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE inputHandle_{};
    };
}
