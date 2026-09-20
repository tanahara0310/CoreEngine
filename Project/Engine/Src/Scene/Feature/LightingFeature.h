#pragma once

#include "ISceneFeature.h"
#include "Graphics/Light/Light.h"

namespace CoreEngine
{
    class GameObject;
    class LightManager;

    /// @brief シーンのライトを束ねる Feature
    /// @details ライト 1 灯は `LightComponent` が持つ。この Feature は
    ///          ①シーンが自前のライトを持たないときのための既定の太陽を 1 灯置き、
    ///          ②毎フレーム全灯ぶんの値を `LightManager` の実体へ写して GPU へ転送する。
    class LightingFeature : public ISceneFeature {
    public:
        /// @brief 既定の太陽の空（大気散乱）輝度スケール
        static constexpr float kDefaultSunAtmosphereIntensity = 20.0f;

        /// @brief 既定の太陽のオブジェクト名
        static constexpr const char* kDefaultSunObjectName = "Sun";

        const char* GetName() const override { return "Lighting"; }

        /// @brief 既定の太陽のオブジェクトを置く
        /// @note シーンのコード（OnInitialize）から GetDirectionalLight() で触れるよう、
        ///       保存データの復元より前のこの時点で置く。
        void Initialize(SceneContext& ctx) override;

        /// @brief シーンが自前の平行光源を持っていたら、既定の太陽を引っ込める
        void PostSceneInitialize(SceneContext& ctx) override;

        /// @brief 全ライトの値を実体へ写し、GPU へ転送する（FrameStart）
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す（止めるとライトのパラメータ編集が画面に出ない）
        bool RunsWhileStopped() const override { return true; }

        /// @brief 大気の太陽（無ければ最初の平行光源）を取得（1 灯も無ければ nullptr）
        Light* GetDirectionalLight() const;

    private:
        /// @brief 既定の太陽のオブジェクトを作る
        void CreateDefaultSun(SceneContext& ctx);

        LightManager* lightManager_ = nullptr;

        /// 既定の太陽のオブジェクト（シーンが自前のライトを持っていたら引っ込めて nullptr）
        GameObject* defaultSun_ = nullptr;
    };
}
