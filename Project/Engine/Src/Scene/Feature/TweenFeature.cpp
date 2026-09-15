#include "pch.h"
#include "TweenFeature.h"
#include "Utility/Tween/TweenManager.h"

namespace CoreEngine
{
    void TweenFeature::Update(SceneContext&, SceneUpdatePhase phase)
    {
        if (phase == SceneUpdatePhase::PreObjectUpdate) {
            TweenManager::GetInstance().Update();
        } else if (phase == SceneUpdatePhase::BetweenObjectUpdates) {
            TweenManager::GetInstance().AdvanceAddedAfterUpdate();
        }
    }

    void TweenFeature::PostSceneFinalize(SceneContext&)
    {
        TweenManager::GetInstance().Clear();
    }
}
