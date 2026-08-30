#include "pch.h"
#include "SampleGameScene.h"

#include "PlayerControllerComponent.h"

#include "Collision/CollisionLayer.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Primitive/CubeMeshGenerator.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Primitive/SphereMeshGenerator.h"

#include <cmath>
#include <numbers>
#include <string>

namespace SampleGame
{
    using namespace CoreEngine;

    namespace {
        constexpr float kPlayerRadius = 0.5f;
        constexpr float kItemSize = 0.8f;
        constexpr float kItemRingRadius = 6.0f;
        constexpr int   kItemCount = 8;
        constexpr float kFieldSize = 20.0f;
    }

    void SampleGameScene::OnInitialize()
    {
        SetSceneName("SampleGameScene");

        // フィールド全体が入るようにゲーム視点カメラを斜め上へ置く
        SetReleaseCameraTransform({ 0.0f, 14.0f, -13.0f }, { 0.75f, 0.0f, 0.0f });
        SetReleaseCameraLens(50.0f, 500.0f);

        // Player × Item は既定で無効なので、ここで有効化する
        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Item, true);

        // ── フィールド（板） ──────────────────────────────
        {
            auto* field = CreateObject("Field");
            field->AddComponent<MeshRendererComponent>(
                std::make_unique<PlaneMeshGenerator>(kFieldSize, kFieldSize));
            field->AddComponent<MaterialComponent>()->SetColor({ 0.18f, 0.20f, 0.24f, 1.0f });

            // 既定の床と Z ファイトしないよう少しだけ持ち上げる
            field->GetComponent<TransformComponent>()->Get().translate = { 0.0f, 0.02f, 0.0f };
            field->SetActive(true);
        }

        // ── プレイヤー（球）──────────────────────────────
        {
            auto* player = CreateObject("Player");
            player->AddComponent<MeshRendererComponent>(
                std::make_unique<SphereMeshGenerator>(kPlayerRadius));
            player->AddComponent<MaterialComponent>();
            player->GetComponent<TransformComponent>()->Get().translate =
                { 0.0f, kPlayerRadius, 0.0f };

            player->AddSphereCollider(kPlayerRadius, CollisionLayer::Player);
            player->AddComponent<PlayerControllerComponent>();
            player->SetActive(true);
        }

        // ── アイテム（立方体）を円形に配置 ────────────────
        for (int i = 0; i < kItemCount; ++i) {
            const float angle = (std::numbers::pi_v<float> *2.0f * i) / kItemCount;

            auto* item = CreateObject("Item_" + std::to_string(i));
            item->AddComponent<MeshRendererComponent>(
                std::make_unique<CubeMeshGenerator>(kItemSize));
            item->AddComponent<MaterialComponent>()->SetColor({ 0.95f, 0.60f, 0.15f, 1.0f });
            item->GetComponent<TransformComponent>()->Get().translate = {
                std::cos(angle) * kItemRingRadius,
                kItemSize * 0.5f,
                std::sin(angle) * kItemRingRadius
            };

            item->AddAABBCollider({ kItemSize, kItemSize, kItemSize }, CollisionLayer::Item);
            item->SetActive(true);
        }
    }
}
