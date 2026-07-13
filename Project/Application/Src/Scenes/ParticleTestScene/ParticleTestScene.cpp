#include "pch.h"
#include "ParticleTestScene.h"
#include "Particle/Modules/MainModule.h"

#ifdef _DEBUG
#include "Editor/Camera/CameraDebugUI.h"
#endif

using namespace CoreEngine;

void ParticleTestScene::OnInitialize()
{
    SetSceneName("ParticleTestScene");

    // ===== パーティクルシステムの初期化（CPU/GPUとも統一入口から生成） =====
    particleSystem_ = CreateParticleSystem(ParticleBackend::CPU, "TestParticle");
    if (particleSystem_) {
        particleSystem_->SetEmitterPosition({ 0.0f, 0.0f, 0.0f });
        particleSystem_->SetBlendMode(BlendMode::kBlendModeAdd);
        particleSystem_->SetBillboardType(BillboardType::ViewFacing);
        particleSystem_->Play();
    }

    // ===== GPUパーティクルシステム（CPU版との比較用に x=+3 へ配置） =====
    gpuParticleSystem_ = CreateParticleSystem(ParticleBackend::GPU, "TestGpuParticle");
    if (gpuParticleSystem_) {
        gpuParticleSystem_->SetEmitterPosition({ 3.0f, 0.0f, 0.0f });
        gpuParticleSystem_->SetBlendMode(BlendMode::kBlendModeAdd);
        // CPU版(白)と区別するためオレンジ
        gpuParticleSystem_->GetMainModule().GetMainData().startColor = { 1.0f, 0.4f, 0.1f, 1.0f };
        gpuParticleSystem_->Play();
    }
}

void ParticleTestScene::OnUpdate()
{

}

void ParticleTestScene::Draw()
{
    // 基底クラスの描画（全てのゲームオブジェクトとパーティクルの描画）
    BaseScene::Draw();
}


