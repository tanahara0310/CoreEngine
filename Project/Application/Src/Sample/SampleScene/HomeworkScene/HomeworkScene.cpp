#include "pch.h"
#include "HomeworkScene.h"

#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Input/InputManager.h"
#include "Particle/ParticleSystem.h"

#include "Sample/TestGameObject/Primitive/PrimitiveSphereObject.h"

#include <dinput.h>

using namespace CoreEngine;

// ヒットエフェクトの再生座標（固定）
static constexpr Vector3 kHitEffectPosition = { 0.0f, 0.0f, 0.0f };

void HomeworkScene::OnInitialize()
{
    SetSceneName("HomeworkScene");

    // ===== 球体を複数配置 =====
    // 3×3 のグリッド状に 9 個配置する
    constexpr int   kCols    = 3;
    constexpr int   kRows    = 3;
    constexpr float kSpacing = 3.0f;
    const float kOriginX = -(kCols - 1) * kSpacing * 0.5f;
    const float kOriginZ = -(kRows - 1) * kSpacing * 0.5f;

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            auto sphere = CreateObject<PrimitiveSphereObject>(0.8f, 32, 16);
            sphere->GetTransform().translate = {
                kOriginX + col * kSpacing,
                0.0f,
                kOriginZ + row * kSpacing
            };
            sphere->SetActive(true);
        }
    }

    // ===== ヒットエフェクトの初期化 =====
    auto* dxCommon        = engine_->GetComponent<DirectXCommon>();
    auto* resourceFactory = engine_->GetComponent<ResourceFactory>();

    auto* ps  = CreateObject<ParticleSystem>();
    hitEffect_ = std::make_unique<HitEffect>();
    hitEffect_->Initialize(ps, dxCommon, resourceFactory);
}

void HomeworkScene::OnUpdate()
{
    // ===== スペースキーで固定座標にヒットエフェクトを再生 =====
    auto* inputManager = engine_->GetComponent<InputManager>();
    if (!inputManager) {
        return;
    }

    if (inputManager->GetQuery().IsKeyTriggered(DIK_SPACE)) {
        hitEffect_->Play(kHitEffectPosition);
    }
}

void HomeworkScene::Draw()
{
    BaseScene::Draw();
}

void HomeworkScene::Finalize()
{
    hitEffect_.reset();

    // ===== グレースケールを無効化して他シーンに影響を与えない =====
    auto* postEffectManager = engine_->GetComponent<PostEffectManager>();
    if (postEffectManager) {
        postEffectManager->SetEffectEnabled(PostEffectNames::GrayScale, false);
    }

    BaseScene::Finalize();
}
