#pragma once

#include "Collision/ColliderComponent.h"
#include "Collision/CollisionInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "GameObject/Sprite/SpriteObject.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>

namespace Sprite2DSample
{
    /// @brief 自機スプライトの左右移動と、アイテム取得時の発色を行うコンポーネント。
    /// @details 描画は SpriteObject 側が持つ。ここはゲームロジックだけを担当する。
    class PaddleComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "Paddle"; }

        void Start() override
        {
            sprite_ = dynamic_cast<CoreEngine::SpriteObject*>(GetOwner());

            GetOwner()->GetColliders().SetOnEnter(
                [this](const CoreEngine::CollisionInfo& info) {
                    if (info.other) { info.other->Destroy(); }
                    ++score_;
                    flashTimer_ = kFlashSeconds;
                    ApplyColor();
                });

            ApplyColor();
        }

        void Update() override
        {
            if (!sprite_) { return; }

            const float deltaTime = CoreEngine::Time::DeltaTime();

            if (const CoreEngine::InputQuery* input = Input()) {
                using CoreEngine::InputAction;

                // 2D でも入力の取り方は 3D と同じ（キーボード・十字キー・左スティック）
                const float moveX = input->GetAxisValue(InputAction::MoveRight)
                    - input->GetAxisValue(InputAction::MoveLeft);

                auto& transform = sprite_->GetSpriteTransform();
                transform.translate.x = std::clamp(
                    transform.translate.x + moveX * kMoveSpeed * deltaTime, -kLimitX, kLimitX);
            }

            if (flashTimer_ > 0.0f) {
                flashTimer_ -= deltaTime;
                if (flashTimer_ <= 0.0f) { ApplyColor(); }
            }
        }

        int GetScore() const { return score_; }

    private:
        static constexpr float kMoveSpeed = 520.0f;   // 2D はピクセル単位なので値が大きい
        static constexpr float kLimitX = 880.0f;
        static constexpr float kFlashSeconds = 0.2f;

        void ApplyColor()
        {
            if (!sprite_) { return; }

            const CoreEngine::Vector4 baseColor{ 0.30f, 0.65f, 0.95f, 1.0f };
            const CoreEngine::Vector4 hitColor{ 0.35f, 0.95f, 0.50f, 1.0f };
            sprite_->SetColor(flashTimer_ > 0.0f ? hitColor : baseColor);
        }

        const CoreEngine::InputQuery* Input() const
        {
            auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
            auto* manager = engine ? engine->GetService<CoreEngine::InputManager>() : nullptr;
            return manager ? &manager->GetQuery() : nullptr;
        }

        CoreEngine::SpriteObject* sprite_ = nullptr;
        float flashTimer_ = 0.0f;
        int score_ = 0;
    };
}
