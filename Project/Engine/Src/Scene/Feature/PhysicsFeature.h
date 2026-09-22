#pragma once

#include "ISceneFeature.h"
#include "Physics/PhysicsWorld.h"

namespace CoreEngine
{
    /// @brief シーンの物理を進める Feature
    /// @details PostObjectUpdate で固定ステップを回す（CollisionFeature より先）。
    class PhysicsFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Physics"; }

        /// @brief シーン初期化時（CVar の取り込みと設定パネルの登録）
        void Initialize(SceneContext& ctx) override;

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief シーン終了時（溜めた時間とステップ数を捨てる）
        void Finalize(SceneContext& ctx) override;

        /// @brief 物理ワールドを取得する
        PhysicsWorld& GetWorld() { return world_; }
        const PhysicsWorld& GetWorld() const { return world_; }

#ifdef CORE_EDITOR
        /// @brief 設定パネルを 1 回だけ登録する
        static void EnsureSettingsPanelRegistered(EngineSystem* engine);

        /// @brief 設定パネルが編集する Feature を差し替える
        static void SetActiveForSettingsPanel(PhysicsFeature* physics);

        /// @brief 設定パネルの中身を描く
        void DrawSettingsImGui();
#endif

    private:
        /// @brief CVar の値をワールドと Time へ反映する
        void ApplyCVars();

        PhysicsWorld world_;
    };
}
