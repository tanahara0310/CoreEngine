#pragma once
#include "RenderPass.h"
#include <d3d12.h>
#include <string>

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

        /// @brief レンダーターゲット名を設定
        /// @param name ターゲット名
        void SetRenderTargetName(const std::string& name) {
            targetName_ = name;
        }

        /// @brief 設定されているターゲット名を取得
        /// @return ターゲット名
        const std::string& GetRenderTargetName() const {
            return targetName_;
        }

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE inputHandle_{};
        std::string targetName_ = "BackBuffer";  ///< デフォルトターゲット名
    };
}
