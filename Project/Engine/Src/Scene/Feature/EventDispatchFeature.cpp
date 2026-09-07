#include "pch.h"
#include "EventDispatchFeature.h"
#include "Utility/Event/EventBus.h"

namespace CoreEngine
{
    void EventDispatchFeature::Update(SceneContext&, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::PostObjectUpdate) {
            return;
        }

        EventBus::GetInstance().DispatchQueued();
    }

    void EventDispatchFeature::PostSceneFinalize(SceneContext&)
    {
        EventBus::GetInstance().Clear();
    }
}
