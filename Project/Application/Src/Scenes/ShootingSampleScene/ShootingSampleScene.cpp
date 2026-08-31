#include "pch.h"
#include "ShootingSampleScene.h"

#include "BulletObject.h"
#include "EnemyObject.h"
#include "ShipControllerComponent.h"

#include "Collision/CollisionLayer.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Primitive/CubeMeshGenerator.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"

#include <memory>

namespace ShootingSample
{
    using namespace CoreEngine;

    namespace {
        constexpr float kFieldWidth = 24.0f;
        constexpr float kFieldDepth = 40.0f;
        constexpr float kFieldCenterZ = 4.0f;

        constexpr float kShipWidth = 1.0f;
        constexpr float kShipHeight = 0.4f;
        constexpr float kShipDepth = 1.4f;
    }

    void ShootingSampleScene::OnInitialize()
    {
        SetSceneName("ShootingSampleScene");

        // 自機の背後上空から奥を見下ろす
        SetReleaseCameraTransform({ 0.0f, 15.0f, -24.0f }, { 0.52f, 0.0f, 0.0f });
        SetReleaseCameraLens(50.0f, 500.0f);

        // 判定するレイヤーの組み合わせは既定で無効なので、ここで開通させる
        SetCollisionEnabled(CollisionLayer::PlayerBullet, CollisionLayer::Enemy, true);
        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy, true);

        // ── フィールド ────────────────────────────────────
        {
            auto* field = CreateObject("Field");
            field->AddComponent<MeshRendererComponent>(
                std::make_unique<PlaneMeshGenerator>(kFieldWidth, kFieldDepth));
            field->AddComponent<MaterialComponent>()->SetColor({ 0.14f, 0.17f, 0.22f, 1.0f });

            // 既定の床と Z ファイトしないよう少しだけ持ち上げる
            field->GetComponent<TransformComponent>()->Get().translate =
                { 0.0f, 0.02f, kFieldCenterZ };
        }

        // ── 自機 ──────────────────────────────────────────
        {
            auto* ship = CreateObject("Ship");
            ship->AddComponent<MeshRendererComponent>(
                std::make_unique<CubeMeshGenerator>(kShipWidth, kShipHeight, kShipDepth));
            ship->AddComponent<MaterialComponent>();
            ship->GetComponent<TransformComponent>()->Get().translate =
                { 0.0f, kShipHeight * 0.5f, -10.0f };

            ship->AddAABBCollider({ kShipWidth, kShipHeight, kShipDepth }, CollisionLayer::Player);
            ship->AddComponent<ShipControllerComponent>();
        }

        // ── 敵スポナー（見た目を持たない管理用オブジェクト）──
        {
            auto* spawner = CreateObject("EnemySpawner");
            spawner->SetSerializeEnabled(false);
            spawner->AddComponent<EnemySpawnerComponent>();
        }
    }
}
