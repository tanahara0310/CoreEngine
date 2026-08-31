#include "pch.h"
#include "Sprite2DSampleScene.h"

#include "FallingItemComponent.h"
#include "PaddleComponent.h"

#include "Collision/CollisionLayer.h"
#include "GameObject/Sprite/SpriteObject.h"
#include "UI/UIImage.h"

namespace Sprite2DSample
{
    using namespace CoreEngine;

    namespace {
        /// 1x1 の白テクスチャ。scale がそのままピクセルサイズになるので矩形として使える
        constexpr const char* kWhiteTexture = "white1x1.png";

        constexpr float kPaddleWidth = 120.0f;
        constexpr float kPaddleHeight = 24.0f;
        constexpr float kPaddleY = -430.0f;
    }

    void Sprite2DSampleScene::OnInitialize()
    {
        SetSceneName("Sprite2DSampleScene");

        // 3D の床や空が映り込まないよう既定の床を止める
        SetDefaultGroundEnabled(false);

        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Item, true);

        // ── 背景（Sprite パス／ワールド座標・画面中央が原点）──
        {
            auto* background = CreateObject<SpriteObject>();
            background->Initialize(kWhiteTexture, "Background");
            background->SetAnchor({ 0.5f, 0.5f });
            background->SetColor({ 0.10f, 0.12f, 0.18f, 1.0f });
            // 2D の可視範囲は「画面解像度そのもの」（中央原点・Y 上正）。
            // 全面を覆って背後の 3D の空を隠す
            background->GetSpriteTransform().scale = { 1920.0f, 1080.0f, 1.0f };

            // 数字が小さいほど奥。背景を最背面に置く
            background->SetSortingLayer(0);
        }

        // ── 自機 ──────────────────────────────────────────
        {
            auto* paddle = CreateObject<SpriteObject>();
            paddle->Initialize(kWhiteTexture, "Paddle");
            paddle->SetAnchor({ 0.5f, 0.5f });
            paddle->SetSortingLayer(1);

            auto& transform = paddle->GetSpriteTransform();
            transform.scale = { kPaddleWidth, kPaddleHeight, 1.0f };
            transform.translate = { 0.0f, kPaddleY, 0.0f };

            paddle->AddAABBCollider({ 1.0f, 1.0f, 100.0f }, CollisionLayer::Player);
            paddle->AddComponent<PaddleComponent>();
        }

        // ── アイテムスポナー（見た目を持たない管理用オブジェクト）──
        {
            auto* spawner = CreateObject("ItemSpawner");
            spawner->SetSerializeEnabled(false);
            spawner->AddComponent<ItemSpawnerComponent>();
        }

        // ── HUD（UI パス／スクリーン座標・左上原点）──
        {
            auto* hud = CreateObject<UIImage>();
            hud->Initialize(kWhiteTexture, "HudBar");
            hud->SetAnchor(UIAnchor::TopLeft);
            hud->SetAnchoredPosition({ 24.0f, 24.0f });
            hud->SetSize({ 220.0f, 12.0f });
            hud->SetColor({ 0.95f, 0.80f, 0.30f, 0.9f });
            hud->SetSortOrder(10);
        }
    }
}
