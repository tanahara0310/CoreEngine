#pragma once

#include "GameObject/Component/Core/IComponent.h"

namespace CoreEngine
{
    class UIText;
}

namespace GameComponents
{
    /// @brief ゲーム開始時の「200ｍすすめ！」案内を左右へ送り出すアニメーション
    class GameStartPromptAnimationComponent final : public CoreEngine::IComponent
    {
    public:
        const char* GetTypeName() const override { return "GameStartPromptAnimation"; }

        void Start() override;
        void OnDestroy() override;

    private:
        CoreEngine::UIText* text_ = nullptr;
    };
}
