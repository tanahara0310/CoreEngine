#pragma once

#include "Collision/ColliderComponent.h"
#include "Collision/CollisionInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/FrameRateController.h"

#include <algorithm>

namespace SampleGame
{
    /// @brief プレイヤーの移動とアイテム取得を行うコンポーネント。
    /// @details 入力で XZ 平面を移動し、触れたアイテムを消してスコアを加算する。
    class PlayerControllerComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "PlayerController"; }

        /// @brief 兄弟コンポーネントの取得と衝突コールバックの購読を行う
        void Start() override
        {
            transform_ = Sibling<CoreEngine::TransformComponent>();
            material_ = Sibling<CoreEngine::MaterialComponent>();

            // GameObject を継承しなくても、コライダーのイベントはここで購読できる
            GetOwner()->GetColliders().SetOnEnter(
                [this](const CoreEngine::CollisionInfo& info) {
                    if (!info.other) { return; }

                    // 非アクティブ化でアイテムが消え、衝突登録からも外れる
                    info.other->SetActive(false);
                    ++score_;
                    flashTimer_ = kFlashSeconds;
                    ApplyColor();
                });

            ApplyColor();
        }

        /// @brief 入力による移動と、取得時の発光の減衰を行う
        void Update() override
        {
            if (!transform_) { return; }

            const float deltaTime = DeltaTime();

            if (const CoreEngine::InputQuery* input = Input()) {
                using CoreEngine::InputAction;

                // アクション経由なのでキーボード・十字キー・左スティックが同時に効く。
                // GetAxisValue はスティックのアナログ値をそのまま返す
                const float moveX = input->GetAxisValue(InputAction::MoveRight)
                    - input->GetAxisValue(InputAction::MoveLeft);
                const float moveZ = input->GetAxisValue(InputAction::MoveForward)
                    - input->GetAxisValue(InputAction::MoveBack);

                // Space / パッド A を押している間は加速する
                const float speed = kMoveSpeed
                    * (input->IsActionPressed(InputAction::Jump) ? kBoostScale : 1.0f);

                auto& world = transform_->Get();
                world.translate.x = std::clamp(
                    world.translate.x + moveX * speed * deltaTime, -kFieldHalf, kFieldHalf);
                world.translate.z = std::clamp(
                    world.translate.z + moveZ * speed * deltaTime, -kFieldHalf, kFieldHalf);
            }

            if (flashTimer_ > 0.0f) {
                flashTimer_ -= deltaTime;
                if (flashTimer_ <= 0.0f) { ApplyColor(); }
            }
        }

        /// @brief 取得したアイテム数
        int GetScore() const { return score_; }

    private:
        static constexpr float kMoveSpeed = 8.0f;
        static constexpr float kBoostScale = 2.0f;
        static constexpr float kFieldHalf = 9.0f;
        static constexpr float kFlashSeconds = 0.25f;

        /// @brief 発光中かどうかで色を切り替える
        void ApplyColor()
        {
            if (!material_) { return; }

            const CoreEngine::Vector4 baseColor{ 0.25f, 0.55f, 0.95f, 1.0f };
            const CoreEngine::Vector4 hitColor{ 0.30f, 0.95f, 0.45f, 1.0f };
            material_->SetColor(flashTimer_ > 0.0f ? hitColor : baseColor);
        }

        CoreEngine::EngineSystem* Engine() const
        {
            return GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
        }

        const CoreEngine::InputQuery* Input() const
        {
            auto* engine = Engine();
            auto* manager = engine ? engine->GetService<CoreEngine::InputManager>() : nullptr;
            return manager ? &manager->GetQuery() : nullptr;
        }

        float DeltaTime() const
        {
            auto* engine = Engine();
            auto* frameRate = engine ? engine->GetService<CoreEngine::FrameRateController>() : nullptr;
            return frameRate ? frameRate->GetDeltaTime() : (1.0f / 60.0f);
        }

        CoreEngine::TransformComponent* transform_ = nullptr;
        CoreEngine::MaterialComponent* material_ = nullptr;
        float flashTimer_ = 0.0f;
        int score_ = 0;
    };
}
