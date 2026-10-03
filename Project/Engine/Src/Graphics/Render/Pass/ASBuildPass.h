#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief DXR 加速構造（BLAS / TLAS）構築パス
    /// @details TLAS 構築と RT シャドウのフレーム状態リセットを含む。
    /// @note frameNumber ガードでフレーム内 1 回だけ動く。
    class ASBuildPass : public RenderPass {
    public:
        ASBuildPass() = default;
        ~ASBuildPass() override = default;

        const char* GetName() const override { return "ASBuild"; }

        void Execute(const RenderContext& context) override;

    private:
        uint64_t lastBuiltFrame_ = UINT64_MAX; ///< 最後に構築したフレーム番号
    };
}
