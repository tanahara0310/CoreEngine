#pragma once

#include "Collision/CollisionLayer.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Primitive/CubeMeshGenerator.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Random/RandomGenerator.h"

#include <memory>

namespace ShootingSample
{
    /// @brief 敵を手前へ直進させ、通り過ぎたら消すコンポーネント。
    class EnemyComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "Enemy"; }

        void Start() override { transform_ = Sibling<CoreEngine::TransformComponent>(); }

        void Update() override
        {
            if (!transform_) { return; }

            auto& world = transform_->Get();
            world.translate.z -= kSpeed * CoreEngine::Time::DeltaTime();

            // 撃ち漏らした敵を消さないと増え続ける
            if (world.translate.z < kDespawnZ) { GetOwner()->Destroy(); }
        }

    private:
        static constexpr float kSpeed = 6.0f;
        static constexpr float kDespawnZ = -16.0f;

        CoreEngine::TransformComponent* transform_ = nullptr;
    };

    /// @brief 敵 1 体分の構成。
    class EnemyObject : public CoreEngine::GameObject {
    public:
        static constexpr float kSize = 1.0f;

        /// @brief Hierarchy の表示名（Enemy_0, Enemy_1 ... と自動採番される）
        const char* GetObjectName() const override { return "Enemy"; }

        void Initialize() override
        {
            AddComponent<CoreEngine::MeshRendererComponent>(
                std::make_unique<CoreEngine::CubeMeshGenerator>(kSize));
            AddComponent<CoreEngine::MaterialComponent>()
                ->SetColor({ 0.90f, 0.30f, 0.28f, 1.0f });

            AddAABBCollider({ kSize, kSize, kSize }, CoreEngine::CollisionLayer::Enemy);
            AddComponent<EnemyComponent>();
        }
    };

    /// @brief 一定間隔で敵を湧かせるコンポーネント。
    /// @details 見た目を持たない空の GameObject に載せて使う。
    class EnemySpawnerComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "EnemySpawner"; }

        void Update() override
        {
            timer_ -= CoreEngine::Time::DeltaTime();
            if (timer_ > 0.0f) { return; }
            timer_ = kInterval;

            // Update 中の Spawn は安全（次フレームから動き始める）
            auto* enemy = GetOwner()->Spawn<EnemyObject>();

            const float x = CoreEngine::RandomGenerator::GetInstance().GetFloat(-kSpreadX, kSpreadX);
            enemy->GetComponent<CoreEngine::TransformComponent>()->Get().translate =
                { x, EnemyObject::kSize * 0.5f, kSpawnZ };
        }

    private:
        static constexpr float kInterval = 0.8f;
        static constexpr float kSpawnZ = 20.0f;
        static constexpr float kSpreadX = 9.0f;

        float timer_ = 0.5f;
    };
}
