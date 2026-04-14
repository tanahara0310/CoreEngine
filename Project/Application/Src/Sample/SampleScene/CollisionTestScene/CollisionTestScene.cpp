#include "CollisionTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputAction.h"
#include "Utility/FrameRate/FrameRateController.h"

using namespace CoreEngine;

void CollisionTestScene::OnInitialize()
{
    SetSceneName("CollisionTestScene");

    // Player ↔ Enemy レイヤー間の衝突を有効化
    SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);

    // ===== 操作用の球体（WASD で移動） =====
    playerSphere_ = CreateObject<SphereObject>();
    playerSphere_->GetTransform().translate = { 0.0f, 1.0f, 0.0f };
    playerSphere_->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    playerSphere_->SetMaterialColor(kPlayerColor);
    playerSphere_->AddSphereCollider(kSphereRadius, CollisionLayer::Player);

    // ===== 静置された球体群 =====
    const Vector3 positions[kStaticSphereCount] = {
        {  4.0f, 1.0f,  0.0f },
        { -4.0f, 1.0f,  0.0f },
        {  0.0f, 1.0f,  4.0f },
        {  0.0f, 1.0f, -4.0f },
    };

    for (int i = 0; i < kStaticSphereCount; ++i) {
        staticSpheres_[i] = CreateObject<SphereObject>();
        staticSpheres_[i]->GetTransform().translate = positions[i];
        staticSpheres_[i]->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        staticSpheres_[i]->SetMaterialColor(kStaticColors[i]);
            staticSpheres_[i]->AddSphereCollider(kSphereRadius, CollisionLayer::Enemy);
            }
        }

void CollisionTestScene::OnUpdate()
{
    auto inputManager = engine_->GetComponent<InputManager>();
    auto frameRate    = engine_->GetComponent<FrameRateController>();
    if (!inputManager || !playerSphere_ || !frameRate) {
        return;
    }

    auto& input = inputManager->GetQuery();
    const float speed = 5.0f * frameRate->GetDeltaTime();
    Vector3 move = { 0.0f, 0.0f, 0.0f };

    if (input.IsActionPressed(InputAction::MoveForward)) { move.z += speed; }
    if (input.IsActionPressed(InputAction::MoveBack))    { move.z -= speed; }
    if (input.IsActionPressed(InputAction::MoveLeft))    { move.x -= speed; }
    if (input.IsActionPressed(InputAction::MoveRight))   { move.x += speed; }

    playerSphere_->GetTransform().translate = playerSphere_->GetTransform().translate + move;
}

void CollisionTestScene::Draw()
{
    // 基底クラスの描画（全ゲームオブジェクトの一括描画）
    BaseScene::Draw();
}

void CollisionTestScene::Finalize()
{
    // 基底クラスの解放
    BaseScene::Finalize();
}

