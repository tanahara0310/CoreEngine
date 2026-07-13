#pragma once

#include "ISceneFeature.h"
#include "Collider/CollisionManager.h"
#include "Collider/CollisionConfig.h"

namespace CoreEngine
{
    /// @brief シーンのコリジョン判定を管理する Feature
    /// @details PostObjectUpdate（GameObject 更新後）で毎フレーム
    ///          収集 → 判定 を実行する。
    class CollisionFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Collision"; }

        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief レイヤー間の衝突判定を有効/無効に設定
        void SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable) {
            collisionConfig_.SetCollisionEnabled(a, b, enable);
        }

    private:
        CollisionConfig collisionConfig_;
        CollisionManager collisionManager_{ &collisionConfig_ };
    };
}
