#pragma once

#include "Collision/CollisionLayer.h"
#include "Collision/ColliderComponent.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/Component/Transform/EulerTransformComponent.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Random/RandomGenerator.h"

namespace Sprite2DSample
{
    /// @brief アイテムを落下させ、画面下へ抜けたら消すコンポーネント。
    class FallingItemComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "FallingItem"; }

        void Start() override { transform_ = Sibling<CoreEngine::EulerTransformComponent>(); }

        void Update() override
        {
            if (!transform_) { return; }

            auto& transform = transform_->Get();
            transform.translate.y -= kFallSpeed * CoreEngine::Time::DeltaTime();

            // 取り逃がしたアイテムを消さないと増え続ける
            if (transform.translate.y < kDespawnY) { GetOwner()->Destroy(); }
        }

    private:
        static constexpr float kFallSpeed = 260.0f;
        static constexpr float kDespawnY = -560.0f;

        CoreEngine::EulerTransformComponent* transform_ = nullptr;
    };

    /// @brief 一定間隔でアイテムのスプライトを生成するコンポーネント。
    /// @details 素の GameObject にトランスフォームとスプライト描画のコンポーネントを付けて作る。
    class ItemSpawnerComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "ItemSpawner"; }

        void Update() override
        {
            timer_ -= CoreEngine::Time::DeltaTime();
            if (timer_ > 0.0f) { return; }
            timer_ = kInterval;

            CoreEngine::GameObject* const item = GetOwner()->Spawn();
            if (!item) { return; }
            item->SetName("Item");
            item->AddComponent<CoreEngine::EulerTransformComponent>();

            auto* sprite = item->AddComponent<CoreEngine::SpriteRendererComponent>("white1x1.png");
            sprite->SetAnchor({ 0.5f, 0.5f });
            sprite->SetColor({ 0.95f, 0.65f, 0.20f, 1.0f });

            auto& transform = item->GetComponent<CoreEngine::EulerTransformComponent>()->Get();
            transform.scale = { kItemSize, kItemSize, 1.0f };
            transform.translate = {
                CoreEngine::RandomGenerator::GetInstance().GetFloat(-kSpreadX, kSpreadX),
                kSpawnY,
                0.0f
            };

            // コライダーのサイズはスプライトの scale が乗る（Z は重なり用に厚みを持たせる）
            item->GetOrAddComponent<CoreEngine::ColliderComponent>()->AddBox(
                { 1.0f, 1.0f, 100.0f }, CoreEngine::CollisionLayer::Item);
            item->AddComponent<FallingItemComponent>();
        }

    private:
        static constexpr float kInterval = 0.7f;
        static constexpr float kItemSize = 32.0f;
        static constexpr float kSpawnY = 560.0f;
        static constexpr float kSpreadX = 880.0f;

        float timer_ = 0.4f;
    };
}
