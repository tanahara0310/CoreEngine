#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"
#include "Utility/CVar/CVar.h"

#include <cstddef>

namespace CoreEngine
{
    class TransformComponent;
}

namespace GameComponents
{
    /// @brief リザルト画面のサルをX軸・Z軸で個別に揺らすコンポーネント。
    class ResultMonkeyShakeComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> XAmplitude;
        static CoreEngine::CVar<float> ZAmplitude;
        static CoreEngine::CVar<float> XInterval;
        static CoreEngine::CVar<float> ZInterval;
        static CoreEngine::CVar<float> XDuration;
        static CoreEngine::CVar<float> ZDuration;
        static CoreEngine::CVar<float> XFrequency;
        static CoreEngine::CVar<float> ZFrequency;

        explicit ResultMonkeyShakeComponent(std::size_t monkeyIndex = 0)
            : monkeyIndex_(monkeyIndex) {}

        const char* GetTypeName() const override { return "ResultMonkeyShake"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "リザルトサル揺れ"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }
        bool DrawInspector() override;
#endif

        void Start() override;
        void Update() override;

    private:
        float UpdateAxis(
            float deltaTime,
            float amplitude,
            float interval,
            float duration,
            float frequency,
            float& intervalTimer,
            float& burstTimer,
            float phase);

        CoreEngine::TransformComponent* transform_ = nullptr;
        CoreEngine::Vector3 baseTranslation_{};
        std::size_t monkeyIndex_ = 0;

        float xIntervalTimer_ = 0.0f;
        float zIntervalTimer_ = 0.0f;
        float xBurstTimer_ = -1.0f;
        float zBurstTimer_ = -1.0f;
        float xPhase_ = 0.0f;
        float zPhase_ = 0.0f;
    };
}
