#include "ParticleEmitter.h"
#include "Particle/ParticleSystem.h" // Particle構造体のため
#include "Particle/Modules/MainModule.h"
#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/ShapeModule.h"
#include "Particle/Modules/VelocityModule.h"
#include "Particle/Modules/RotationModule.h"



namespace CoreEngine
{
void ParticleEmitter::Initialize(
    MainModule* mainModule,
    EmissionModule* emissionModule,
    ShapeModule* shapeModule,
    VelocityModule* velocityModule,
    RotationModule* rotationModule
) {
    mainModule_ = mainModule;
    emissionModule_ = emissionModule;
    shapeModule_ = shapeModule;
    velocityModule_ = velocityModule;
    rotationModule_ = rotationModule;
}

uint32_t ParticleEmitter::EmitParticles(
    uint32_t count,
    const EulerTransform& emitterTransform,
    uint32_t maxParticles,
    std::list<Particle>& outParticles
) {
    uint32_t emittedCount = 0;

    for (uint32_t i = 0; i < count && outParticles.size() < maxParticles; ++i) {
        Particle newParticle = CreateParticle(emitterTransform);
        outParticles.push_back(newParticle);
        ++emittedCount;
    }

    return emittedCount;
}

Particle ParticleEmitter::CreateParticle(const EulerTransform& emitterTransform) {
    Particle particle;

    // === MainModuleから初期値を設定（最優先） ===
    if (mainModule_ && mainModule_->IsEnabled()) {
        particle.transform.scale = mainModule_->GenerateStartSize();
        particle.initialScale = particle.transform.scale;  // 初期サイズを保存（サイズ変化用）
        particle.transform.rotate = mainModule_->GenerateStartRotation();
        particle.color = mainModule_->GenerateStartColor();
        particle.initialColor = particle.color;  // 初期色を保存（グラデーション用）
        particle.lifeTime = mainModule_->GenerateStartLifetime();
        particle.currentTime = 0.0f;
    }

    // ShapeModuleから位置を生成
    if (shapeModule_ && shapeModule_->IsEnabled()) {
        particle.transform.translate = shapeModule_->GeneratePosition(emitterTransform.translate);
    }

    // === 速度の設定 ===
    // VelocityModuleが方向を決定し、MainModuleが大きさを適用
    if (velocityModule_ && velocityModule_->IsEnabled() && mainModule_ && mainModule_->IsEnabled()) {
        velocityModule_->ApplyInitialVelocity(particle);
        float startSpeed = mainModule_->GenerateStartSpeed();

        // 正規化された方向ベクトルに速度の大きさを掛ける
             // （VelocityModuleが既に正規化済みのため、直接スカラー倍）
        particle.velocity = CoreEngine::MathCore::Vector::Multiply(startSpeed, particle.velocity);
    }

    // === 回転速度の設定 ===
    // RotationModuleは回転速度のみを設定（初期回転はMainModuleが設定済み）
    if (rotationModule_ && rotationModule_->IsEnabled()) {
        rotationModule_->ApplyInitialRotation(particle);
    }

    return particle;
}
}
