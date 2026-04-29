#include "SpriteTestScene.h"

#include "Input/KeyboardInput.h"
#include "Input/InputManager.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"
#include "Camera/CameraManager.h"
#include "ObjectCommon/Sprite/SpriteAnimationClip.h"

using namespace CoreEngine;
using namespace CoreEngine::MathCore;

namespace
{
    // タイルマップ設定
    constexpr int   kTileMapCols = 10;
    constexpr int   kTileMapRows = 10;
    constexpr float kTileSize = 32.0f;

    // タイルID
    constexpr uint8_t kTileEmpty = 0;
    constexpr uint8_t kTileWall = 1;

    // 10x10 マップ（外周が壁、内部にいくつか壁を配置）
    // 1=壁, 0=空白
    constexpr uint8_t kInitialMap[kTileMapRows][kTileMapCols] = {
        {1,1,1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,0,0,1},
        {1,0,1,0,0,0,0,1,0,1},
        {1,0,1,0,0,0,0,1,0,1},
        {1,0,0,0,1,1,0,0,0,1},
        {1,0,0,0,1,1,0,0,0,1},
        {1,0,1,0,0,0,0,1,0,1},
        {1,0,1,0,0,0,0,1,0,1},
        {1,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1},
    };
}

void SpriteTestScene::OnInitialize()
{
    SetSceneName("SpriteTestScene");

    // ────────────────────────────────────────────────────────────────
    // 既存のテストスプライトは非表示にする（コードは残しておく）
    // ────────────────────────────────────────────────────────────────
    spriteCenter_ = CreateObject<SpriteObject>();
    spriteCenter_->Initialize("uvChecker.png", "Center_uvChecker");
    spriteCenter_->SetActive(false);

    spriteTopLeft_ = CreateObject<SpriteObject>();
    spriteTopLeft_->Initialize("checker_black.png", "TopLeft_checkerBlack");
    spriteTopLeft_->SetActive(false);

    spriteTinted_ = CreateObject<SpriteObject>();
    spriteTinted_->Initialize("uvChecker.png", "Tinted_Red");
    spriteTinted_->SetActive(false);

    spriteScaled_ = CreateObject<SpriteObject>();
    spriteScaled_->Initialize("white1x1.png", "Scaled_Bar");
    spriteScaled_->SetActive(false);

    spriteAnim_ = CreateObject<SpriteObject>();
    spriteAnim_->Initialize("0.png", "Anim_Number");
    spriteAnim_->SetActive(false);

    // ────────────────────────────────────────────────────────────────
    // タイルマップ＆プレイヤーの初期化
    // ────────────────────────────────────────────────────────────────
    BuildTileMap();
    InitializePlayer();
    InitializeCanvas();

    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::General,
        "{}", "SpriteTestScene: 初期化完了（タイルマップテストモード）");
}

void SpriteTestScene::BuildTileMap()
{
    // マップを 10x10 で作成し、画面中央に配置する
    // 全体サイズ = 10 * 32 = 320px
    // 左上ワールド座標 = (-160, +160) で中央配置
    const float halfMapW = (kTileMapCols * kTileSize) * 0.5f;
    const float halfMapH = (kTileMapRows * kTileSize) * 0.5f;
    const Vector2 mapOrigin = { -halfMapW, +halfMapH };

    tileMap_.Initialize(kTileMapCols, kTileMapRows, kTileSize, mapOrigin);
    int collisionLayer = tileMap_.AddLayer("collision");

    // タイルデータを流し込みつつ、見た目用スプライトも生成する
    for (int row = 0; row < kTileMapRows; ++row) {
        for (int col = 0; col < kTileMapCols; ++col) {
            uint8_t id = kInitialMap[row][col];
            tileMap_.SetTile(collisionLayer, col, row, id);

            // 描画用スプライト（white1x1 を 32px に拡大）
            auto* sprite = CreateObject<SpriteObject>();
            sprite->Initialize("white1x1.png",
                std::string("Tile_") + std::to_string(col) + "_" + std::to_string(row));

            // タイル中央のワールド座標
            Vector2 tileCenter = tileMap_.TileToWorld(col, row);
            sprite->GetSpriteTransform().translate = { tileCenter.x, tileCenter.y, 0.0f };
            sprite->GetSpriteTransform().scale = { kTileSize, kTileSize, 1.0f };
            sprite->SetAnchor({ 0.5f, 0.5f });

            // 壁=濃いグレー、空白=薄いグレー（区別できるよう色分け）
            if (id == kTileWall) {
                sprite->SetColor({ 0.25f, 0.25f, 0.30f, 1.0f });
            } else {
                sprite->SetColor({ 0.75f, 0.75f, 0.80f, 1.0f });
            }

            // タイルは背景なので Sorting Layer を低く設定
            sprite->SetSortingLayer(0);
            sprite->SetActive(true);

            tileSprites_.push_back(sprite);
        }
    }

    // 衝突解決クラスにマップを登録
    tileCollider_.SetTileMap(&tileMap_, collisionLayer);
    tileCollider_.SetHalfSize(playerHalfW_, playerHalfH_);
}

void SpriteTestScene::InitializePlayer()
{
    player_ = CreateObject<SpriteObject>();
    player_->Initialize("white1x1.png", "Player");
    // プレイヤーは AABB の幅と高さに合わせる
    player_->GetSpriteTransform().scale = { playerHalfW_ * 2.0f, playerHalfH_ * 2.0f, 1.0f };
    player_->SetAnchor({ 0.5f, 0.5f });
    player_->SetColor({ 1.0f, 0.4f, 0.2f, 1.0f }); // オレンジ色で目立たせる
    // タイルより前面
    player_->SetSortingLayer(10);

    // 内部の空白タイル中央あたりに初期配置（col=4, row=4 付近）
    Vector2 startPos = tileMap_.TileToWorld(4, 4);
    playerPos_ = startPos;
    player_->GetSpriteTransform().translate = { playerPos_.x, playerPos_.y, 0.0f };
    player_->SetActive(true);
}

void SpriteTestScene::OnUpdate()
{
    if (!player_) { return; }

    // ── 入力取得 ─────────────────────────────────────────────────
    auto* input = engine_->GetComponent<InputManager>();
    if (!input) { return; }
    const auto& q = input->GetQuery();

    // WASD / 矢印キーで移動
    Vector2 dir = { 0.0f, 0.0f };
    if (q.IsKeyPressed(DIK_A) || q.IsKeyPressed(DIK_LEFT)) { dir.x -= 1.0f; }
    if (q.IsKeyPressed(DIK_D) || q.IsKeyPressed(DIK_RIGHT)) { dir.x += 1.0f; }
    if (q.IsKeyPressed(DIK_W) || q.IsKeyPressed(DIK_UP)) { dir.y += 1.0f; }
    if (q.IsKeyPressed(DIK_S) || q.IsKeyPressed(DIK_DOWN)) { dir.y -= 1.0f; }

    // 斜め移動の正規化
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len > 0.0001f) {
        dir.x /= len;
        dir.y /= len;
    }

    playerVelocity_.x = dir.x * playerSpeed_;
    playerVelocity_.y = dir.y * playerSpeed_;

    // ── 移動 → 衝突解決（旧座標と速度を渡す）─────────────────────
    auto result = tileCollider_.Resolve(playerPos_, playerVelocity_);
    playerPos_      = result.resolvedPosition;
    playerVelocity_ = result.velocity;

    // スプライトに反映
    player_->GetSpriteTransform().translate = { playerPos_.x, playerPos_.y, 0.0f };

    // Q/E キーで HP バーを増減（UI 動作確認）
    if (q.IsKeyPressed(DIK_Q)) { hpRatio_ = hpRatio_ - 0.01f < 0.0f ? 0.0f : hpRatio_ - 0.01f; }
    if (q.IsKeyPressed(DIK_E)) { hpRatio_ = hpRatio_ + 0.01f > 1.0f ? 1.0f : hpRatio_ + 0.01f; }
    if (uiHpBarFill_) {
        // 200px 幅の HP バーを残量に応じて伸縮
        uiHpBarFill_->SetSize({ 200.0f * hpRatio_, 20.0f });
    }
}

void SpriteTestScene::InitializeCanvas()
{
    canvas_.Initialize(engine_, { 0.0f, 0.0f });

    auto make = [&](UIImage*& img, const char* name) -> UIImage* {
        img = CreateObject<UIImage>();
        img->Initialize("white1x1.png", name);
        return img;
    };

    // HP バー（左上）
    make(uiHpBarBG_, "HPBar_BG");
    uiHpBarBG_->SetAnchor(UIAnchor::TopLeft);
    uiHpBarBG_->SetPivot({ 0.0f, 0.0f });
    uiHpBarBG_->SetAnchoredPosition({ 20.0f, 20.0f });
    uiHpBarBG_->SetSize({ 204.0f, 24.0f });
    uiHpBarBG_->SetColor({ 0.1f, 0.1f, 0.1f, 0.8f });
    uiHpBarBG_->SetSortOrder(0);

    make(uiHpBarFill_, "HPBar_Fill");
    uiHpBarFill_->SetAnchor(UIAnchor::TopLeft);
    uiHpBarFill_->SetPivot({ 0.0f, 0.0f });
    uiHpBarFill_->SetAnchoredPosition({ 22.0f, 22.0f });
    uiHpBarFill_->SetSize({ 200.0f, 20.0f });
    uiHpBarFill_->SetColor({ 0.9f, 0.2f, 0.2f, 1.0f });
    uiHpBarFill_->SetSortOrder(1);

    // 四隅マーカー
    auto corner = [&](UIImage*& img, UIAnchor anchor, Vector2 pos, Vector2 pivot, Vector4 col, const char* name) {
        make(img, name);
        img->SetAnchor(anchor);
        img->SetPivot(pivot);
        img->SetAnchoredPosition(pos);
        img->SetSize({ 30.0f, 30.0f });
        img->SetColor(col);
    };
    corner(uiCornerTL_, UIAnchor::TopLeft,     {  10.0f,  60.0f }, { 0.0f, 0.0f }, { 1,1,0,1 }, "Corner_TL");
    corner(uiCornerTR_, UIAnchor::TopRight,    { -10.0f,  10.0f }, { 1.0f, 0.0f }, { 0,1,1,1 }, "Corner_TR");
    corner(uiCornerBL_, UIAnchor::BottomLeft,  {  10.0f, -10.0f }, { 0.0f, 1.0f }, { 1,0,1,1 }, "Corner_BL");
    corner(uiCornerBR_, UIAnchor::BottomRight, { -10.0f, -10.0f }, { 1.0f, 1.0f }, { 0,1,0,1 }, "Corner_BR");

    // 中央マーカー
    make(uiCenterMark_, "CenterMark");
    uiCenterMark_->SetAnchor(UIAnchor::Center);
    uiCenterMark_->SetPivot({ 0.5f, 0.5f });
    uiCenterMark_->SetAnchoredPosition({ 0.0f, 0.0f });
    uiCenterMark_->SetSize({ 8.0f, 8.0f });
    uiCenterMark_->SetColor({ 1.0f, 1.0f, 1.0f, 0.8f });
}

void SpriteTestScene::Draw()
{

    BaseScene::Draw();
}

void SpriteTestScene::Finalize()
{
    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::General,
        "{}", "SpriteTestScene: 終了");
}
