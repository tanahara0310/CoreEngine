#pragma once

#include "Collision/CollisionLayer.h"
#include "Collision/ColliderComponent.h"
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

    /// @brief 敵 1 体の大きさ
    inline constexpr float kEnemySize = 1.0f;

    /// @brief 敵を 1 体作る（見た目・コライダー・動きのコンポーネントを載せる）
    /// @param spawner 同じシーンへ作るための呼び出し元
    inline CoreEngine::GameObject* SpawnEnemy(CoreEngine::GameObject& spawner)
    {
        CoreEngine::GameObject* const enemy = spawner.Spawn();
        if (!enemy) {
            return nullptr;
        }
        enemy->SetName("Enemy");
        enemy->AddComponent<CoreEngine::MeshRendererComponent>(
            std::make_unique<CoreEngine::CubeMeshGenerator>(kEnemySize));
        enemy->AddComponent<CoreEngine::MaterialComponent>()
            ->SetColor({ 0.90f, 0.30f, 0.28f, 1.0f });
        enemy->GetOrAddComponent<CoreEngine::ColliderComponent>()->AddBox(
            { kEnemySize, kEnemySize, kEnemySize }, CoreEngine::CollisionLayer::Enemy);
        enemy->AddComponent<EnemyComponent>();
        return enemy;
    }

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

            // Update 中に作っても安全（次フレームから動き始める）
            CoreEngine::GameObject* const enemy = SpawnEnemy(*GetOwner());
            if (!enemy) { return; }

            const float x = CoreEngine::RandomGenerator::GetInstance().GetFloat(-kSpreadX, kSpreadX);
            enemy->GetComponent<CoreEngine::TransformComponent>()->Get().translate =
                { x, kEnemySize * 0.5f, kSpawnZ };
        }

    private:
        static constexpr float kInterval = 0.8f;
        static constexpr float kSpawnZ = 20.0f;
        static constexpr float kSpreadX = 9.0f;

        float timer_ = 0.5f;
    };
}
