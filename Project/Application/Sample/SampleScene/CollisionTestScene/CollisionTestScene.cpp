#include "CollisionTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Engine/Input/KeyboardInput.h"
#include "Engine/Utility/FrameRate/FrameRateController.h"

using namespace CoreEngine;

void CollisionTestScene::Initialize(EngineSystem* engine)
{
    // 基底クラスの初期化（カメラ、ライト、グリッド）
    BaseScene::Initialize(engine);
    SetSceneName("CollisionTestScene");

    // Player ↔ Enemy レイヤー間の衝突を有効化
    SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);

    // ===== 操作用の球体（WASD で移動） =====
    playerSphere_ = CreateObject<SphereObject>();
    playerSphere_->Initialize();
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
        staticSpheres_[i]->Initialize();
        staticSpheres_[i]->GetTransform().translate = positions[i];
        staticSpheres_[i]->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
        staticSpheres_[i]->SetMaterialColor(kStaticColors[i]);
        staticSpheres_[i]->AddSphereCollider(kSphereRadius, CollisionLayer::Enemy);
    }

    // JSON からオブジェクトのトランスフォームを復元（ファイルがなければコード値をそのまま使用）
    LoadObjectsFromJson();
}

void CollisionTestScene::OnUpdate()
{
    // キーボード入力で操作用球体を移動
    auto keyboard  = engine_->GetComponent<KeyboardInput>();
    auto frameRate = engine_->GetComponent<FrameRateController>();
    if (!keyboard || !playerSphere_ || !frameRate) {
        return;
    }

    const float speed = 5.0f * frameRate->GetDeltaTime();
    Vector3 move = { 0.0f, 0.0f, 0.0f };

    if (keyboard->IsKeyPressed(DIK_W)) { move.z += speed; }
    if (keyboard->IsKeyPressed(DIK_S)) { move.z -= speed; }
    if (keyboard->IsKeyPressed(DIK_A)) { move.x -= speed; }
    if (keyboard->IsKeyPressed(DIK_D)) { move.x += speed; }

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

