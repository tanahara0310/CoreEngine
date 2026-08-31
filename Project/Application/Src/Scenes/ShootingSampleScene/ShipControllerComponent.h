#pragma once

#include "BulletObject.h"

#include "Collision/ColliderComponent.h"
#include "Collision/CollisionInfo.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>

namespace ShootingSample
{
    /// @brief 自機の移動・弾の発射・被弾表現を行うコンポーネント。
    class ShipControllerComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "ShipController"; }

        /// @brief 兄弟コンポーネントの取得と被弾コールバックの購読を行う
        void Start() override
        {
            transform_ = Sibling<CoreEngine::TransformComponent>();
            material_ = Sibling<CoreEngine::MaterialComponent>();

            GetOwner()->GetColliders().SetOnEnter(
                [this](const CoreEngine::CollisionInfo& info) {
                    if (info.other) { info.other->Destroy(); }
                    damageFlash_ = kFlashSeconds;
                    ApplyColor();
                });

            ApplyColor();
        }

        /// @brief 移動・発射・被弾表現の減衰を行う
        void Update() override
        {
            if (!transform_) { return; }

            const float deltaTime = CoreEngine::Time::DeltaTime();
            fireCooldown_ -= deltaTime;

            if (const CoreEngine::InputQuery* input = Input()) {
                using CoreEngine::InputAction;

                const float moveX = input->GetAxisValue(InputAction::MoveRight)
                    - input->GetAxisValue(InputAction::MoveLeft);
                const float moveZ = input->GetAxisValue(InputAction::MoveForward)
                    - input->GetAxisValue(InputAction::MoveBack);

                auto& world = transform_->Get();
                world.translate.x = std::clamp(
                    world.translate.x + moveX * kMoveSpeed * deltaTime, -kLimitX, kLimitX);
                world.translate.z = std::clamp(
                    world.translate.z + moveZ * kMoveSpeed * deltaTime, kMinZ, kMaxZ);

                // Attack = マウス左 / パッドX。押しっぱなしで連射する
                if (input->IsActionPressed(InputAction::Attack) && fireCooldown_ <= 0.0f) {
                    fireCooldown_ = kFireInterval;
                    Fire();
                }
            }

            if (damageFlash_ > 0.0f) {
                damageFlash_ -= deltaTime;
                if (damageFlash_ <= 0.0f) { ApplyColor(); }
            }
        }

    private:
        static constexpr float kMoveSpeed = 12.0f;
        static constexpr float kLimitX = 10.0f;
        static constexpr float kMinZ = -13.0f;
        static constexpr float kMaxZ = 2.0f;
        static constexpr float kFireInterval = 0.16f;
        static constexpr float kBulletSpeed = 26.0f;
        static constexpr float kFlashSeconds = 0.25f;

        /// @brief 自機の前方へ弾を 1 発スポーンする
        void Fire()
        {
            auto* bullet = GetOwner()->Spawn<BulletObject>();

            bullet->GetComponent<CoreEngine::TransformComponent>()->Get().translate =
                transform_->Get().translate + CoreEngine::Vector3{ 0.0f, 0.0f, 1.0f };
            bullet->GetComponent<BulletComponent>()->SetVelocity(
                { 0.0f, 0.0f, kBulletSpeed });
        }

        /// @brief 被弾中かどうかで色を切り替える
        void ApplyColor()
        {
            if (!material_) { return; }

            const CoreEngine::Vector4 baseColor{ 0.30f, 0.65f, 0.95f, 1.0f };
            const CoreEngine::Vector4 hitColor{ 0.95f, 0.35f, 0.25f, 1.0f };
            material_->SetColor(damageFlash_ > 0.0f ? hitColor : baseColor);
        }

        const CoreEngine::InputQuery* Input() const
        {
            auto* engine = GetOwner() ? GetOwner()->GetEngineSystem() : nullptr;
            auto* manager = engine ? engine->GetService<CoreEngine::InputManager>() : nullptr;
            return manager ? &manager->GetQuery() : nullptr;
        }

        CoreEngine::TransformComponent* transform_ = nullptr;
        CoreEngine::MaterialComponent* material_ = nullptr;
        float fireCooldown_ = 0.0f;
        float damageFlash_ = 0.0f;
    };
}
