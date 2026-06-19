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
        std::string targetName_ = "BackBuffer";  ///< デフォルトターゲット名
    };
}
