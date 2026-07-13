#pragma once

#include "ISceneFeature.h"
#include "Graphics/Light/LightData.h"

namespace CoreEngine
{
    /// @brief 既定ディレクショナルライトとシャドウマップ用行列を管理する Feature
    /// @details Initialize でシーン既定の太陽光（大気散乱の太陽を兼ねる）を生成し、
    ///          FrameStart で LightManager の更新とシャドウマップ用
    ///          View-Projection 行列の反映を行う。
    class LightingFeature : public ISceneFeature {
    public:
        /// @brief シャドウマップ設定
        struct Config {
            float shadowLightDistance = 50.0f; ///< ライトの距離
            float shadowOrthoSize = 50.0f;     ///< 正射影範囲
            float shadowNearPlane = 0.1f;      ///< 近平面
            float shadowFarPlane = 100.0f;     ///< 遠平面
        };

        const char* GetName() const override { return "Lighting"; }

        void Initialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 既定ディレクショナルライトを取得（未生成時は nullptr）
        DirectionalLightData* GetDirectionalLight() const { return directionalLight_; }

        Config& GetConfig() { return config_; }

    private:
        /// @brief シャドウマップ用のライト View-Projection 行列を更新
        void UpdateLightViewProjection(SceneContext& ctx);

        Config config_{};
        DirectionalLightData* directionalLight_ = nullptr;
    };
}
