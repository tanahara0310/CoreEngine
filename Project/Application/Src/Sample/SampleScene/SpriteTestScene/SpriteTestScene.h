#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "ObjectCommon/Sprite/SpriteObject.h"
#include "ObjectCommon/Sprite/SpriteAnimationClip.h"
#include "TileMap/TileMap.h"
#include "TileMap/TileCollider.h"
#include <memory>
#include <vector>

/// @brief スプライト機能テスト用シーン
class SpriteTestScene : public CoreEngine::BaseScene {
public:
    /// @brief シーン固有の初期化
    void OnInitialize() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新
    void OnUpdate() override;

private:
    /// @brief タイルマップを構築（10x10）してスプライトを配置
    void BuildTileMap();

    /// @brief プレイヤーの初期化
    void InitializePlayer();

private:
    // ── 既存のテスト用スプライト（現在は非表示） ──────────────────
    CoreEngine::SpriteObject* spriteCenter_ = nullptr;
    CoreEngine::SpriteObject* spriteTopLeft_ = nullptr;
    CoreEngine::SpriteObject* spriteTinted_ = nullptr;
    CoreEngine::SpriteObject* spriteScaled_ = nullptr;
    CoreEngine::SpriteObject* spriteAnim_ = nullptr;

    // ── タイルマップ関連 ──────────────────────────────────────
    CoreEngine::TileMap tileMap_;
    CoreEngine::TileCollider tileCollider_;
    std::vector<CoreEngine::SpriteObject*> tileSprites_; ///< 描画用スプライト群

    // ── プレイヤー ──────────────────────────────────────────
    CoreEngine::SpriteObject* player_ = nullptr;
    CoreEngine::Vector2 playerPos_ = { 0.0f, 0.0f };
    CoreEngine::Vector2 playerVelocity_ = { 0.0f, 0.0f };
    float playerSpeed_ = 4.0f;   ///< 速度（px/frame）
    float playerHalfW_ = 14.0f;  ///< AABB 横半サイズ
    float playerHalfH_ = 14.0f;  ///< AABB 縦半サイズ
};
