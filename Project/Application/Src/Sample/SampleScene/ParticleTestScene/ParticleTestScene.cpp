#include "ParticleTestScene.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"

#ifdef _DEBUG
#include "Camera/Debug/Editor/CameraDebugUI.h"
#endif

using namespace CoreEngine;

void ParticleTestScene::Initialize(EngineSystem* engine)
{
    // 基底クラスの初期化（カメラ、ライト、グリッドのセットアップ）
    BaseScene::Initialize(engine);
    SetSceneName("ParticleTestScene");

    // コンポーネントを取得
    auto dxCommon = engine_->GetComponent<DirectXCommon>();
    auto resourceFactory = engine_->GetComponent<ResourceFactory>();

    if (!dxCommon || !resourceFactory) {
        return; // 必須コンポーネントがない場合は終了
    }

    // ===== パーティクルシステムの初期化 =====
    auto particleSystem = CreateObject<ParticleSystem>();
    particleSystem->Initialize(dxCommon, resourceFactory, "TestParticle");

    // パーティクルシステムの基本設定
    particleSystem->SetEmitterPosition({ 0.0f, 0.0f, 0.0f });
    particleSystem->SetBlendMode(BlendMode::kBlendModeAdd);
    particleSystem->SetBillboardType(BillboardType::ViewFacing);

    particleSystem_ = particleSystem;

    // パーティクルを再生開始
    particleSystem_->Play();

    // JSON からオブジェクトのトランスフォームを復元（ファイルがなければコード値をそのまま使用）
    LoadObjectsFromJson();
}

void ParticleTestScene::OnUpdate()
{

}

void ParticleTestScene::Draw()
{
    // 基底クラスの描画（全てのゲームオブジェクトとパーティクルの描画）
    BaseScene::Draw();
}

void ParticleTestScene::Finalize()
{
    // 基底クラスの解放
    BaseScene::Finalize();
}

