#pragma once

#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"
#include "ObjectCommon/Sprite/SpriteObject.h"
#include "ObjectCommon/Sprite/SpriteAnimationClip.h"
#include "TileMap/TileMap.h"
#include "TileMap/TileCollider.h"
#include "UI/UICanvas.h"
#include "UI/UIImage.h"
#include <memory>
#include <vector>

/// @brief スプライト機能テスト用シーン
class SpriteTestScene : public CoreEngine::BaseScene {
public:
    void OnInitialize() override;
    void Finalize() override;
    void Draw() override;

protected:
    void OnUpdate() override;

private:
    void BuildTileMap();
    void InitializePlayer();
    void InitializeCanvas();

private:
    CoreEngine::SpriteObject* spriteCenter_  = nullptr;
    CoreEngine::SpriteObject* spriteTopLeft_ = nullptr;
    CoreEngine::SpriteObject* spriteTinted_  = nullptr;
    CoreEngine::SpriteObject* spriteScaled_  = nullptr;
    CoreEngine::SpriteObject* spriteAnim_    = nullptr;

    CoreEngine::TileMap tileMap_;
    CoreEngine::TileCollider tileCollider_;
    std::vector<CoreEngine::SpriteObject*> tileSprites_;

    CoreEngine::SpriteObject* player_       = nullptr;
    CoreEngine::Vector2 playerPos_          = { 0.0f, 0.0f };
    CoreEngine::Vector2 playerVelocity_     = { 0.0f, 0.0f };
    float playerSpeed_  = 4.0f;
    float playerHalfW_  = 14.0f;
    float playerHalfH_  = 14.0f;

    // ── UI ──────────────────────────────────────────────────────
    CoreEngine::UICanvas canvas_;
    CoreEngine::UIImage* uiHpBarBG_    = nullptr;
    CoreEngine::UIImage* uiHpBarFill_  = nullptr;
    CoreEngine::UIImage* uiCornerTL_   = nullptr;
    CoreEngine::UIImage* uiCornerTR_   = nullptr;
    CoreEngine::UIImage* uiCornerBL_   = nullptr;
    CoreEngine::UIImage* uiCornerBR_   = nullptr;
    CoreEngine::UIImage* uiCenterMark_ = nullptr;
    float hpRatio_ = 1.0f;
};
