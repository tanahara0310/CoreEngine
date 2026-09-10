#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Utility/CVar/CVar.h"

#include <functional>
#include <utility>

namespace CoreEngine
{
    class TransformComponent;
}

namespace GameComponents
{
    /// @brief trolley.obj の到着後、monkey.obj を OutBack でトロッコ上へ出すコンポーネント。
    class TitleMonkeyAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> Distance;
        static CoreEngine::CVar<CoreEngine::Vector3> Rotation;
        static CoreEngine::CVar<float> IntroDuration;
        static CoreEngine::CVar<float> IntroOffset;

        const char* GetTypeName() const override { return "TitleMonkeyAnimation"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "サル演出設定"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }

        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.48f;
            outRgba[1] = 0.76f;
            outRgba[2] = 0.32f;
            outRgba[3] = 1.0f;
        }

        /// @brief Title.Monkey.* CVar の自動生成UIを描画する。
        /// @return CVar の値が変更されたら true
        bool DrawInspector() override;
#endif

        void Start() override;
        void OnDestroy() override;

        void SetOnIntroComplete(std::function<void()> callback)
        {
            onIntroComplete_ = std::move(callback);
        }

    private:
        CoreEngine::TransformComponent* transform_ = nullptr;
        CoreEngine::Vector3 basePosition_{};
        float introDuration_ = 0.5f;
        float introOffset_ = 1.5f;

        std::function<void()> onIntroComplete_;
        bool introCompleteNotified_ = false;
    };
}
