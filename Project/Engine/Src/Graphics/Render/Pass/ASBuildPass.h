#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief DXR 加速構造（BLAS / TLAS）構築パス
    /// @details 旧 EngineSystem::ExecuteRenderPipeline 直呼びの
    ///          RayTracingSubsystem::BuildAccelerationStructures を Graph ノード化したもの。
    ///          TLAS 構築と RT シャドウのフレーム状態リセットを含むため、
    ///          frameNumber ガードで「フレーム内 1 回」のみ実行する
    ///          （Graph は View ごとに実行されるが、本パスは最初の View でだけ動く）。
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
