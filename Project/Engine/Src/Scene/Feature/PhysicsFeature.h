#pragma once

#include "ISceneFeature.h"
#include "Physics/Debug/PhysicsDebugRenderer.h"
#include "Physics/PhysicsWorld.h"

#include <memory>

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

        // ===== 重力（CVar sys.Physics.Gravity が唯一の持ち主） =====
        // シーンが無くても読み書きできるよう静的にしてある。

        /// @brief 重力加速度（m/s²）
        static Vector3 GetGravity();

        /// @brief 重力加速度を設定する（次のフレームから効く）
        static void SetGravity(const Vector3& gravity);

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

        /// @brief 剛体を集め直し、その持ち主のコライダーへ物理が扱う印を立てる
        void CollectBodies(SceneContext& ctx);

        /// @brief 今のシーンの衝突ワールド（当たり判定を持たないシーンなら nullptr）
        static CollisionWorld* FindCollisionWorld(SceneContext& ctx);

        PhysicsWorld world_;

        /// 速度と接触点のワイヤ表示（この Feature が所有し、Line パスへはポインタを渡すだけ）
        std::unique_ptr<PhysicsDebugRenderer> debugRenderer_;
    };
}
