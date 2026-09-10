#pragma once

#include "Graphics/Light/Light.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    class EngineSystem;
    class GameObject;
    class LightManager;
}

namespace GameScene
{
    /// @brief ステージの夜の灯り（ポイントライト）を受け持つ Feature
    /// @details レールビルダー（赤い矢印）とトロッコを名前で引いて、その位置へ灯りを重ねる。
    ///          対象のコンポーネントには手を入れず、GameObject::GetWorldPosition() だけを読む。
    /// @note 点灯量は太陽ライトの高度角から直接求める。TimeOfDayFeature へは依存しないので、
    ///       サイクルを外して Sky Atmosphere エディタで太陽を手で沈めても同じように灯る。
    /// @note 更新は PostObjectUpdate。GameObject の移動が確定した後に灯りを重ねないと、
    ///       走っているトロッコの後ろに灯りが 1 フレーム置き去りになる。
    class StageLightsFeature : public CoreEngine::ISceneFeature {
    public:
        const char* GetName() const override { return "StageLights"; }

        void Initialize(CoreEngine::SceneContext& ctx) override;

        /// @brief 追従灯の位置と明るさを更新する（PostObjectUpdate）
        void Update(CoreEngine::SceneContext& ctx, CoreEngine::SceneUpdatePhase phase) override;

        /// @brief 生成した灯りを破棄する
        void Finalize(CoreEngine::SceneContext& ctx) override;

        /// @brief 停止中も回す（止めると時刻を動かしても灯りが変わらない）
        bool RunsWhileStopped() const override { return true; }

        /// @brief 現在の点灯量（0 = 消灯 / 1 = 全灯）
        float GetLitRatio() const { return litRatio_; }

#ifdef USE_IMGUI
        /// @brief Inspector に「Stage Lights」パネルを登録する（プロセスで一度だけ）
        static void EnsureSettingsPanelRegistered(CoreEngine::EngineSystem* engine);

        /// @brief このステージをパネルの編集対象にする（nullptr で解除）
        static void SetActiveForSettingsPanel(StageLightsFeature* stage);

        /// @brief 設定パネルの中身を描画する
        void DrawSettingsImGui();
#endif

    private:
        /// @brief 追従灯 1 つぶんの設定（CVar から毎フレーム組み立てる）
        struct FollowLightSettings {
            const char* objectName = "";  ///< 追いかける GameObject 名
            const char* lightName = "";   ///< ライト編集 UI に出す名前
            bool enable = true;
            CoreEngine::Vector3 offset{ 0.0f, 2.0f, 0.0f };
            CoreEngine::Vector3 color{ 1.0f, 1.0f, 1.0f };
            float intensity = 0.0f;
            float range = 8.0f;
        };

        /// @brief 対象の GameObject へ灯りを重ねる（対象が居なければ消灯したまま待つ）
        void UpdateFollowLight(CoreEngine::SceneContext& ctx, CoreEngine::LightManager& lightManager,
            CoreEngine::LightHandle& handle, const FollowLightSettings& settings);

        /// @brief 名前で GameObject を引く（生きているものだけ）
        static const CoreEngine::GameObject* FindObjectByName(
            CoreEngine::SceneContext& ctx, const char* name);

        /// @brief 太陽の高度角 [deg] を求める（太陽が無いシーンは昼として扱う）
        static float ComputeSunElevationDeg(CoreEngine::LightManager& lightManager);

        /// @brief 太陽高度から点灯量（0-1）を求める
        static float ComputeLitRatio(float sunElevationDeg);

        CoreEngine::LightHandle builderLight_{};  ///< レールビルダー（赤い矢印）の灯り
        CoreEngine::LightHandle trainLight_{};    ///< トロッコの灯り

        float litRatio_ = 0.0f;         ///< 直近の点灯量（パネル表示用）
        float sunElevationDeg_ = 90.0f; ///< 直近の太陽高度 [deg]（パネル表示用）

        // 直近に置いた灯りのワールド座標（パネル表示用）。
        // Development ビルドではライトのギズモが出ない（_DEBUG 限定）ため、
        // 画面に出ている光がどれなのかを数字で確かめられるようにしておく
        CoreEngine::Vector3 builderLightPos_{};
        CoreEngine::Vector3 trainLightPos_{};
    };
}
