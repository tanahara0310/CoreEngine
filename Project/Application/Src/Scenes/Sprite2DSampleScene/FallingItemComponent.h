#pragma once

#include "Collision/CollisionLayer.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Sprite/SpriteObject.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Random/RandomGenerator.h"

namespace Sprite2DSample
{
    /// @brief アイテムを落下させ、画面下へ抜けたら消すコンポーネント。
    class FallingItemComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "FallingItem"; }

        void Start() override { sprite_ = dynamic_cast<CoreEngine::SpriteObject*>(GetOwner()); }

        void Update() override
        {
            if (!sprite_) { return; }

            auto& transform = sprite_->GetSpriteTransform();
            transform.translate.y -= kFallSpeed * CoreEngine::Time::DeltaTime();

            // 取り逃がしたアイテムを消さないと増え続ける
            if (transform.translate.y < kDespawnY) { GetOwner()->Destroy(); }
        }

    private:
        static constexpr float kFallSpeed = 260.0f;
        static constexpr float kDespawnY = -560.0f;

        CoreEngine::SpriteObject* sprite_ = nullptr;
    };

    /// @brief 一定間隔でアイテムのスプライトを生成するコンポーネント。
    /// @details 2D は 3D と違い、生成後に Initialize() を明示的に呼ぶ必要がある。
    class ItemSpawnerComponent : public CoreEngine::IComponent {
    public:
        const char* GetTypeName() const override { return "ItemSpawner"; }

        void Update() override
        {
            timer_ -= CoreEngine::Time::DeltaTime();
            if (timer_ > 0.0f) { return; }
            timer_ = kInterval;

            // SpriteObject も GameObject なので Spawn できる
            auto* item = GetOwner()->Spawn<CoreEngine::SpriteObject>();

            // 3D の MeshRendererComponent と違い、テクスチャ指定は Initialize で行う
            item->Initialize("white1x1.png", "Item");
            item->SetAnchor({ 0.5f, 0.5f });
            item->SetColor({ 0.95f, 0.65f, 0.20f, 1.0f });

            auto& transform = item->GetSpriteTransform();
            transform.scale = { kItemSize, kItemSize, 1.0f };
            transform.translate = {
                CoreEngine::RandomGenerator::GetInstance().GetFloat(-kSpreadX, kSpreadX),
                kSpawnY,
                0.0f
            };

            // コライダーのサイズはスプライトの scale が乗る（Z は重なり用に厚みを持たせる）
            item->AddAABBCollider({ 1.0f, 1.0f, 100.0f }, CoreEngine::CollisionLayer::Item);
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
