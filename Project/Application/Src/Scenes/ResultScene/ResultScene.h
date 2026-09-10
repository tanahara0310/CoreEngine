#pragma once

#include "Audio/SoundInstance.h"
#include "Scene/BaseScene.h"
#include "Scenes/ResultScene/ResultSceneUi.h"

#include <cstddef>

namespace CoreEngine
{
    class UIText;
}

namespace GameComponents
{
    class ResultTipsComponent;
}

namespace ResultScene
{
    class ResultScene : public CoreEngine::BaseScene {
    public:
        ResultScene() = default;
        ~ResultScene() override;

        void OnInitialize() override;

        void OnUpdate() override;

        void OnLateUpdate() override;

    private:
        enum class Selection { Retry, Title };

        void SetSelection(Selection selection, bool playReaction);
        void ConfirmSelection();
        void UpdateResultCamera();
        void InitializeTipText();

        Selection selection_ = Selection::Retry;
        bool returnRequested_ = false;
        ResultSceneUi::Elements ui_;
        GameComponents::ResultTipsComponent* resultTips_ = nullptr;
        bool tipInitialized_ = false;
        CoreEngine::ScopedSound resultBgm_;
        float resultCameraOrbitAngle_ = 0.0f;

        inline static std::size_t nextTipIndex_ = 0;
    };
}
