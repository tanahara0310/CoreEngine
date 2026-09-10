#include "pch.h"
#include "SpeedGaugeFeature.h"

#include "Components/Train/TrainMovementComponent.h"
#include "Components/UI/SpeedGaugeUIComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/UIImage.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace
{
    constexpr const char* kBoardTexture =
        "Application/Assets/Textures/Stamina/board_mid.png";

    /// @brief 速度計の土台だけを生成し、あとは SpeedGaugeUIComponent に任せる
    class SpeedGaugeFeature final : public ISceneFeature
    {
    public:
        const char* GetName() const override { return "SpeedGauge"; }

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
                    "SpeedGaugeFeature: TrainMovementComponent が見つからないため速度計を出しません");
                return;
            }

            auto* board = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>());
            if (!board) {
                return;
            }
            board->Initialize(kBoardTexture, "SpeedGaugeBoard");
            board->SetSerializeEnabled(false);
            board->AddComponent<GameComponents::SpeedGaugeUIComponent>(train);
        }

        /// @note 生成した UI はシーンの GameObject と一緒に破棄されるので、後始末は不要
        bool RunsWhileStopped() const override { return true; }
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateSpeedGaugeFeature()
{
    return std::make_unique<SpeedGaugeFeature>();
}
