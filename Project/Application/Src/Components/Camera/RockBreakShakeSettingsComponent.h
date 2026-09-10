#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Utility/CVar/CVar.h"

namespace GameComponents
{
    /// @brief サルが岩を壊し切った瞬間のカメラシェイクと、その調整用 CVar を持つ。
    /// @details 揺れは静的メソッドから鳴らすので、このコンポーネント自体は
    ///          CVar をインスペクタへ出すためだけに存在する。CameraManager へ付ける。
    class RockBreakShakeSettingsComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> Strength;
        static CoreEngine::CVar<float> Duration;
        static CoreEngine::CVar<float> Frequency;

        /// @brief 岩が砕けた瞬間の揺れを鳴らす
        /// @details Strength が 0、またはシーンに揺れの受け手が居なければ何もしない。
        static void PlayRockBreak();

        const char* GetTypeName() const override { return "RockBreakShakeSettings"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "岩破壊のカメラシェイク"; }
        bool DrawInspector() override;
#endif
    };
}
