#pragma once

#include "Audio/SoundInstance.h"
#include "Scene/BaseScene.h"

#include <functional>

namespace CoreEngine
{
    class UIText;
}

namespace TitleScene
{
    /// @brief ゲーム起動時に表示するタイトルシーン
    class TitleScene final : public CoreEngine::BaseScene {
    public:
        void OnInitialize() override;
        void OnUpdate() override;

    private:
        void StartGame();
        void OnTitleIntroAnimationComplete();
        void StartTitleBgmIfReady();

        CoreEngine::UIText* startHint_ = nullptr;
        bool gamepadConnected_ = false;
        bool startRequested_ = false;

        int pendingIntroAnimations_ = 0;
        bool introAnimationRegistrationComplete_ = false;
        bool titleBgmStarted_ = false;
        CoreEngine::ScopedSound titleBgm_;
    };
}
