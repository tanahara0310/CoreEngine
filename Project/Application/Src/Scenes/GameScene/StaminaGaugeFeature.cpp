#include "pch.h"
#include "StaminaGaugeFeature.h"

#include "Components/GameCore/HungerComponent.h"
#include "Components/Rail/RailBuilderComponent.h"
#include "Components/UI/StaminaGaugeUIComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "UI/UIImage.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace
{
    constexpr const char* kBoardTexture =
        "Application/Assets/Textures/Stamina/board_mid.png";

    /// @brief バナナゲージの土台だけを生成し、あとは StaminaGaugeUIComponent に任せる
    class StaminaGaugeFeature final : public ISceneFeature
    {
    public:
        const char* GetName() const override { return "StaminaGauge"; }

        /// @details シーンの OnInitialize() が終わった後に呼ばれるフックなので、
        ///          この時点なら HungerComponent が既に生成されている。
        void PostSceneInitialize(SceneContext& ctx) override
        {
            if (!ctx.gameObjectManager) {
                return;
            }

            auto* hunger =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::HungerComponent>();
            if (!hunger) {
                Logger::GetInstance().Warnf(
                    LogCategory::Game,
                    "StaminaGaugeFeature: HungerComponent が見つからないためゲージを出しません");
                return;
            }

            auto* board = ctx.gameObjectManager->AddObject(std::make_unique<UIImage>());
            if (!board) {
                return;
            }
            board->Initialize(kBoardTexture, "StaminaGaugeBoard");
            board->SetSerializeEnabled(false);
            // 予告表示に使う。見つからなくてもゲージ自体は動く
            auto* builder =
                ctx.gameObjectManager->FindFirstComponent<GameComponents::RailBuilderComponent>();
            board->AddComponent<GameComponents::StaminaGaugeUIComponent>(hunger, builder);
        }

        /// @note 生成した UI はシーンの GameObject と一緒に破棄されるので、後始末は不要
        bool RunsWhileStopped() const override { return true; }
    };
}

std::unique_ptr<CoreEngine::ISceneFeature> GameComponents::CreateStaminaGaugeFeature()
{
    return std::make_unique<StaminaGaugeFeature>();
}
