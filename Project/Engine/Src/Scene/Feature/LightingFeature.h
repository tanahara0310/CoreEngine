#pragma once

#include "ISceneFeature.h"
#include "Graphics/Light/Light.h"

namespace CoreEngine
{
    class LightManager;

    /// @brief 既定ディレクショナルライトを管理する Feature
    /// @details Initialize でシーン既定の太陽光（大気散乱の太陽を兼ねる）を生成し、
    ///          FrameStart で LightManager の更新を行う。
    ///          ライトはハンドルで保持し、参照時に毎回 LightManager から引き直す
    ///          （エディタでの削除後も安全にアクセスできる）。
    class LightingFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Lighting"; }

        void Initialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 既定ディレクショナルライトを取得（未生成・削除済みの場合は nullptr）
        Light* GetDirectionalLight() const;

    private:
        LightManager* lightManager_ = nullptr;
        LightHandle directionalLightHandle_{};
    };
}
