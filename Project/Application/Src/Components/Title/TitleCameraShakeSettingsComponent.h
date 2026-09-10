#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Utility/CVar/CVar.h"

namespace GameComponents
{
    /// @brief タイトルモデルに属さないカメラシェイク用 CVar を表示するコンポーネント。
    /// @details TitleCameraShakeSettings という空の GameObject に付けて使用する。
    class TitleCameraShakeSettingsComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> ShakeStrength;

        const char* GetTypeName() const override { return "TitleCameraShakeSettings"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "タイトルカメラシェイク設定"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }

        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.94f;
            outRgba[1] = 0.56f;
            outRgba[2] = 0.22f;
            outRgba[3] = 1.0f;
        }

        /// @brief Title.CameraShake.* CVar の自動生成UIを描画する。
        /// @return CVar の値が変更されたら true
        bool DrawInspector() override;
#endif
    };
}
