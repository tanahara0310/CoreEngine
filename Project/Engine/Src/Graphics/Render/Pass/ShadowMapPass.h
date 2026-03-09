#pragma once
#include "RenderPass.h"
#include "Math/Vector/Vector3.h"

namespace CoreEngine
{
    /// @brief シャドウマップ生成パス
    class ShadowMapPass : public RenderPass {
    public:
        ShadowMapPass() = default;
        ~ShadowMapPass() override = default;

        const char* GetName() const override { return "ShadowMap"; }

        void Execute(const RenderContext& context) override;

        /// @brief シーンの中心と半径を設定
        /// @param center シーンの中心座標
        /// @param radius シーンの半径
        void SetSceneParams(const Vector3& center, float radius) {
            sceneCenter_ = center;
            sceneRadius_ = radius;
        }

    private:
        Vector3 sceneCenter_ = Vector3(0.0f, 5.0f, 0.0f);
        float sceneRadius_ = 30.0f;
    };
}
