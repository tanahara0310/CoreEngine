#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

#include <functional>

namespace CoreEngine
{
    class UIImage;
}

namespace GameComponents
{
    /// @brief タイトル画面のスタートボタンの登場・選択・押下演出を制御するコンポーネント
    class TitleButtonAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        const char* GetTypeName() const override { return "TitleButtonAnimation"; }

        void Start() override;
        void OnDestroy() override;

        void SetIntroDelay(float seconds) { introDelay_ = seconds; }
        void SetIntroDuration(float seconds) { introDuration_ = seconds; }
        void SetSelectedColor(const CoreEngine::Vector4& color) { selectedColor_ = color; }

        void SetSelected(bool selected);
        void PlayPressAnimation(std::function<void()> onFinished = {});

    private:
        void StartIdleAnimation();

        CoreEngine::UIImage* button_ = nullptr;
        CoreEngine::Vector2 targetSize_{ 320.0f, 80.0f };
        CoreEngine::Vector4 normalColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
        CoreEngine::Vector4 selectedColor_{ 0.30f, 0.70f, 1.0f, 1.0f };

        float introDelay_ = 0.55f;
        float introDuration_ = 0.35f;
        bool selected_ = false;
        bool started_ = false;
    };
}
