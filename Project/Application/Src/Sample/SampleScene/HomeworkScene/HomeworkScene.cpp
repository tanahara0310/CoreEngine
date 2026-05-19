#include "HomeworkScene.h"

#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"

#include "Sample/TestGameObject/Primitive/PrimitiveSphereObject.h"

using namespace CoreEngine;

void HomeworkScene::OnInitialize()
{
    SetSceneName("HomeworkScene");

    // ===== ポストエフェクト: グレースケールを有効化 =====
    auto* postEffectManager = engine_->GetComponent<PostEffectManager>();
    if (postEffectManager) {
        postEffectManager->SetEffectEnabled(PostEffectNames::GrayScale, true);
    }

    // ===== 球体を複数配置 =====
    // 3×3 のグリッド状に 9 個配置する
    constexpr int   kCols = 3;
    constexpr int   kRows = 3;
    constexpr float kSpacing = 3.0f;
    const float kOriginX = -(kCols - 1) * kSpacing * 0.5f;
    const float kOriginZ = -(kRows - 1) * kSpacing * 0.5f;

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            auto sphere = CreateObject<PrimitiveSphereObject>(0.8f, 32u, 16u);
            sphere->GetTransform().translate = {
                kOriginX + col * kSpacing,
                0.0f,
                kOriginZ + row * kSpacing
            };
            sphere->SetActive(true);
        }
    }
}

void HomeworkScene::OnUpdate()
{
}

void HomeworkScene::Draw()
{
    BaseScene::Draw();
}

void HomeworkScene::Finalize()
{
    // ===== グレースケールを無効化して他シーンに影響を与えない =====
    auto* postEffectManager = engine_->GetComponent<PostEffectManager>();
    if (postEffectManager) {
        postEffectManager->SetEffectEnabled(PostEffectNames::GrayScale, false);
    }

    BaseScene::Finalize();
}
