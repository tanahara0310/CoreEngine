#include "pch.h"
#include "OffscreenTrainIndicatorFeature.h"

#include "Components/GameCore/GameManagerComponent.h"
#include "Components/Train/TrainMovementComponent.h"
#include "Components/UI/OffscreenTrainIndicatorUIComponent.h"

#include "Camera/CameraManager.h"
#include "Camera/CameraStructs.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/UIImage.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace
{
    /// アイコンに使うトロッコの絵。ローディング画面と同じ版下を流用する
    constexpr const char* kIconTexture =
        "Application/Assets/Textures/loading_cart.png";

    /// @brief アイコンの土台だけを生成し、あとは OffscreenTrainIndicatorUIComponent に任せる
    class OffscreenTrainIndicatorFeature final : public ISceneFeature
    {
    public:
        const char* GetName() const override { return "OffscreenTrainIndicator"; }

        /// @details シーンの OnInitialize() が終わった後に呼ばれるフックなので、
        ///          この時点なら TrainMovementComponent が既に生成されている。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }

            auto* train =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::TrainMovementComponent>();
            if (!train) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "OffscreenTrainIndicatorFeature: TrainMovementComponent が見つからないため案内を出しません");
                return;
            }

            // 画面内かどうかは、ゲーム視点カメラで測る。エディタのカメラで覗いている
            // あいだも判定はゲーム側のままになるが、これは画面外への移動を止めている
            // RailBuilderComponent::SetViewCamera と同じ扱いで、両者の「画面」がずれない
            Camera* gameCamera = ctx.cameraManager
                ? ctx.cameraManager->GetCamera(CameraNames::Game)
                : nullptr;
            if (!gameCamera) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "OffscreenTrainIndicatorFeature: ゲーム視点カメラが無いため案内を出しません");
                return;
            }

            auto* gameManager =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::GameManagerComponent>();

            auto* icon = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>());
            if (!icon) {
                return;
            }
            icon->Initialize(kIconTexture, "OffscreenTrainIndicator");
            icon->SetSerializeEnabled(false);
            icon->AddComponent<GameComponents::OffscreenTrainIndicatorUIComponent>(
                train, gameCamera, gameManager);
        }

        /// @note 生成した UI はシーンの GameObject と一緒に破棄されるので、後始末は不要
        bool RunsWhileStopped() const override { return true; }
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateOffscreenTrainIndicatorFeature()
{
    return std::make_unique<OffscreenTrainIndicatorFeature>();
}
