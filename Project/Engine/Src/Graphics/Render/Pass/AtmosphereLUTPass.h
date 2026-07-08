#pragma once

#include "RenderPass.h"

namespace CoreEngine
{
    /// @brief 大気散乱 LUT 生成パス（Compute）
    /// @details AtmosphereManager のダーティフラグが立っている場合のみ
    ///          Transmittance / Multi-Scattering / Sky-View LUT を再計算する。
    ///          ShadowMapPass の後・GBufferPass の前に実行される。
    class AtmosphereLUTPass : public RenderPass {
    public:
        AtmosphereLUTPass() = default;
        ~AtmosphereLUTPass() override = default;

        /// @brief パス名を取得
        const char* GetName() const override { return "AtmosphereLUT"; }

        /// @brief パスの実行
        /// @param context レンダリングコンテキスト
        void Execute(const RenderContext& context) override;
    };
}
