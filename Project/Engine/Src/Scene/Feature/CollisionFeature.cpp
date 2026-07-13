#include "pch.h"
#include "CollisionFeature.h"
#include "GameObject/GameObjectManager.h"

namespace CoreEngine
{
    void CollisionFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        // コリジョン判定（毎フレーム: 収集 → 判定）
        // ClearColliders()でコライダーリストのみリセット
        collisionManager_.ClearColliders();
        ctx.gameObjectManager->RegisterAllColliders(&collisionManager_);
        collisionManager_.CheckAllCollisions();
    }
}
