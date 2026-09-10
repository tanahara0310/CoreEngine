#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include "Utility/CVar/CVar.h"

#include <functional>
#include <string>
#include <utility>

namespace CoreEngine
{
    class UIText;
}

namespace GameComponents
{
    /// @brief タイトル画面の UIText のフェード・スライドを制御するコンポーネント
    class TitleTextAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        static CoreEngine::CVar<float> FontSize;
        static CoreEngine::CVar<CoreEngine::Vector2> Position;
        static CoreEngine::CVar<CoreEngine::Vector4> Color;
        static CoreEngine::CVar<int> SortOrder;
        static CoreEngine::CVar<float> IntroDelay;
        static CoreEngine::CVar<float> SlideDistance;
        static CoreEngine::CVar<float> IntroDuration;
        static CoreEngine::CVar<float> IdleScale;
        static CoreEngine::CVar<float> IdleDuration;
        static CoreEngine::CVar<float> StartReactionScale;
        static CoreEngine::CVar<float> StartReactionDuration;

        explicit TitleTextAnimationComponent(
            std::string tweenId = "title_text_intro")
            : tweenId_(std::move(tweenId)) {
        }

        const char* GetTypeName() const override { return "TitleTextAnimation"; }

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "タイトル操作ヒント設定"; }
        const char* GetInspectorIcon() const override { return "scene.png"; }

        void GetInspectorIconColor(float* outRgba) const override
        {
            outRgba[0] = 0.94f;
            outRgba[1] = 0.56f;
            outRgba[2] = 0.22f;
            outRgba[3] = 1.0f;
        }

        /// @brief Title.UI.* CVar の自動生成UIを描画する。
        /// @return CVar の値が変更されたら true
        bool DrawInspector() override;
#endif

        void Start() override;
        void OnDestroy() override;

        void SetOnIntroComplete(std::function<void()> callback)
        {
            onIntroComplete_ = std::move(callback);
        }

        /// @brief スタート入力時にテキストを拡大してから縮小しながら消す
        void PlayStartReaction(std::function<void()> onFinished = {});

    private:
        void NotifyIntroComplete();
        void StartIdleAnimation();

        CoreEngine::UIText* text_ = nullptr;
        std::string tweenId_;
        float delay_ = 0.0f;
        float slideDistance_ = 24.0f;
        float duration_ = 0.45f;
        float idleScale_ = 1.025f;
        float idleDuration_ = 1.15f;
        float reactionScale_ = 1.35f;
        float reactionDuration_ = 0.25f;
        float baseFontSize_ = 32.0f;
        std::function<void()> onIntroComplete_;
        bool introCompleteNotified_ = false;
        bool startReactionStarted_ = false;
    };
}
