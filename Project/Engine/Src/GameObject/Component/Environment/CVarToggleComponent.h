#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
/// @brief 有効・無効を bool の CVar へ写すコンポーネントの土台
/// @details 結んでいる間は、OnEnable / OnDisable で CVar を書く。
///          結ぶのは持ち主の Feature で、オブジェクトを壊す前に外す（シーンを閉じても CVar は変わらない）。
class CVarToggleComponent : public IComponent {
public:
    /// @brief 有効・無効を写す先（nullptr で外す）
    void BindEnabledCVar(CVar<bool>* cvar) { enabledCVar_ = cvar; }

    void OnEnable() override
    {
        if (enabledCVar_) {
            enabledCVar_->Set(true);
        }
    }

    void OnDisable() override
    {
        if (enabledCVar_) {
            enabledCVar_->Set(false);
        }
    }

private:
    CVar<bool>* enabledCVar_ = nullptr;
};
}
