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
    /// @brief trolley.obj とその上の monkey.obj を画面下から登場させるコンポーネント。
    class TitleTrolleyAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<CoreEngine::Vector3> Position;
        static CoreEngine::CVar<CoreEngine::Vector3> Rotation;
        static CoreEngine::CVar<float> IntroDelay;
        static CoreEngine::CVar<float> IntroDuration;
        static CoreEngine::CVar<float> IntroOffset;
        static CoreEngine::CVar<CoreEngine::Vector3> BobStart;
        static CoreEngine::CVar<CoreEngine::Vector3> BobEnd;
        static CoreEngine::CVar<float> BobDuration;

        const char* GetTypeName() const override { return "TitleTrolleyAnimation"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "トロッコ演出設定"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }

        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.94f;
            outRgba[1] = 0.56f;
            outRgba[2] = 0.22f;
            outRgba[3] = 1.0f;
        }

        /// @brief Title.Trolley.* CVar の自動生成UIを描画する。
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
        void StartIdleAnimation();

        CoreEngine::TransformComponent* transform_ = nullptr;
        CoreEngine::Vector3 basePosition_{};
        float introDuration_ = 0.9f;
        float introOffset_ = 8.0f;
        CoreEngine::Vector3 bobStart_{};
        CoreEngine::Vector3 bobEnd_{ 0.0f, 0.12f, 0.0f };
        float bobDuration_ = 1.6f;

        std::function<void()> onIntroComplete_;
        bool introCompleteNotified_ = false;
    };
}
