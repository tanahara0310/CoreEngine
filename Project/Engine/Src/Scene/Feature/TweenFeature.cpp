#include "pch.h"
#include "TweenFeature.h"
#include "Utility/Tween/TweenManager.h"

namespace CoreEngine
{
    void TweenFeature::Update(SceneContext&, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PreObjectUpdate) {
            return;
        }

        TweenManager::GetInstance().Update();
    }

    void TweenFeature::PostSceneFinalize(SceneContext&)
    {
        TweenManager::GetInstance().Clear();
    }
}
