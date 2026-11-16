#pragma once

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"

namespace CoreEngine
{
    /// @brief コンポーネントの持ち主のエンジンからサービスを引く
    /// @tparam T サービスの型（T のヘッダは呼び出し側で include する）
    /// @return 持ち主・エンジン・サービスのどれかが無ければ nullptr
    template <typename T>
    T* FindService(const IComponent& component)
    {
        const GameObject* const owner = component.GetOwner();
        EngineSystem* const engine = owner ? owner->GetEngineSystem() : nullptr;
        return engine ? engine->GetService<T>() : nullptr;
    }
}
