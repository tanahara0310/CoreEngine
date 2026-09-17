#pragma once

#include "Collision/ColliderComponent.h"
#include "Collision/CollisionInfo.h"
#include "Collision/CollisionLayer.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Primitive/SphereMeshGenerator.h"
#include "Math/Vector/Vector3.h"
#include "Utility/FrameRate/Time.h"

#include <memory>

namespace ShootingSample
{
    /// @brief 弾の直進・寿命・命中処理を行うコンポーネント。
    class BulletComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "Bullet"; }

        void SetVelocity(const CoreEngine::Vector3& velocity) { velocity_ = velocity; }

        /// @brief 命中時に相手と自分を消すコールバックを購読する
        void Start() override
        {
            transform_ = Sibling<CoreEngine::TransformComponent>();

            GetOwner()->GetOrAddComponent<CoreEngine::ColliderComponent>()->SetOnEnter(
                [this](const CoreEngine::CollisionInfo& info) {
                    if (info.other) { info.other->Destroy(); }
                    GetOwner()->Destroy();
                });
        }

        /// @brief 直進させ、寿命が尽きたら自分を消す
        void Update() override
        {
            if (!transform_) { return; }

            const float deltaTime = CoreEngine::Time::DeltaTime();
            transform_->Get().translate += velocity_ * deltaTime;

            // 消し忘れると画面外へ出た弾が残り続ける
            life_ -= deltaTime;
            if (life_ <= 0.0f) { GetOwner()->Destroy(); }
        }

    private:
        CoreEngine::Vector3 velocity_{ 0.0f, 0.0f, 0.0f };
        float life_ = 2.5f;
        CoreEngine::TransformComponent* transform_ = nullptr;
    };

    /// @brief 弾 1 発分の構成。
    /// @details プレハブが無いため、このクラスが弾の定義書になる。
    /// @brief 弾の半径
    inline constexpr float kBulletRadius = 0.15f;

    /// @brief 弾を 1 発作る（見た目・コライダー・動きのコンポーネントを載せる）
    /// @param spawner 同じシーンへ作るための呼び出し元
    inline CoreEngine::GameObject* SpawnBullet(CoreEngine::GameObject& spawner)
    {
        CoreEngine::GameObject* const bullet = spawner.Spawn();
        if (!bullet) {
            return nullptr;
        }
        bullet->SetName("Bullet");
        bullet->AddComponent<CoreEngine::MeshRendererComponent>(
            std::make_unique<CoreEngine::SphereMeshGenerator>(kBulletRadius));
        bullet->AddComponent<CoreEngine::MaterialComponent>()
            ->SetColor({ 1.00f, 0.90f, 0.35f, 1.0f });
        bullet->GetOrAddComponent<CoreEngine::ColliderComponent>()->AddSphere(
            kBulletRadius, CoreEngine::CollisionLayer::PlayerBullet);
        bullet->AddComponent<BulletComponent>();
        return bullet;
    }
}
