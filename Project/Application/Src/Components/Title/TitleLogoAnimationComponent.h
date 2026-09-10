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
    /// @brief title.obj の登場・待機アニメーションを制御するコンポーネント
    class TitleLogoAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<CoreEngine::Vector3> Position;
        static CoreEngine::CVar<CoreEngine::Vector3> Rotation;
        static CoreEngine::CVar<CoreEngine::Vector3> Scale;
        static CoreEngine::CVar<float> IntroDuration;
        static CoreEngine::CVar<float> IntroScale;
        static CoreEngine::CVar<float> DropHeight;
        static CoreEngine::CVar<float> BobHeight;
        static CoreEngine::CVar<float> BobDuration;
        static CoreEngine::CVar<float> RotationAmplitude;

        const char* GetTypeName() const override { return "TitleLogoAnimation"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "タイトルモデル設定"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }

        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.94f;
            outRgba[1] = 0.56f;
            outRgba[2] = 0.22f;
            outRgba[3] = 1.0f;
        }

        /// @brief Title.Transform.* と Title.Animation.* の CVar UI を描画する。
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
        void OnIntroProgress(float progress);
        void PlayBounceShake(float intensity);
        void StartIdleAnimation();

        CoreEngine::TransformComponent* transform_ = nullptr;

        CoreEngine::Vector3 basePosition_{};
        CoreEngine::Vector3 baseRotation_{};
        CoreEngine::Vector3 baseScale_{ 1.0f, 1.0f, 1.0f };

        float introDuration_ = 0.65f;
        float introScale_ = 0.82f;
        float dropHeight_ = 3.5f;
        float bobHeight_ = 0.12f;
        float bobDuration_ = 1.6f;
        float rotationAmplitude_ = 0.035f;
        float shakeStrength_ = 1.0f;

        // EaseOutBounce の接地ポイント（大きい着地 1 回 + 小さい反発 3 回）を
        // それぞれ一度だけ処理するためのインデックス。
        int nextBounceIndex_ = 0;

        std::function<void()> onIntroComplete_;
        bool introCompleteNotified_ = false;
    };
}
