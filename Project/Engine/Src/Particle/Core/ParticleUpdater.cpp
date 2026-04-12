#include "ParticleUpdater.h"
#include "Particle/ParticleSystem.h" // Particle構造体のため
#include "Particle/Modules/ForceModule.h"
#include "Particle/Modules/ColorModule.h"
#include "Particle/Modules/SizeModule.h"
#include "Particle/Modules/RotationModule.h"
#include "Particle/Modules/NoiseModule.h"

#include <algorithm>
#include <execution>


namespace CoreEngine
{
void ParticleUpdater::Initialize(
    ForceModule* forceModule,
    ColorModule* colorModule,
    SizeModule* sizeModule,
    RotationModule* rotationModule,
    NoiseModule* noiseModule
) {
    forceModule_ = forceModule;
    colorModule_ = colorModule;
    sizeModule_ = sizeModule;
    rotationModule_ = rotationModule;
    noiseModule_ = noiseModule;
}

uint32_t ParticleUpdater::UpdateParticles(
    std::vector<Particle>& particles,
    float deltaTime,
    float gravityModifier
) {
    // Phase 1: 並列更新
    // 各パーティクルは独立しているため par_unseq で安全に並列実行できる
    std::for_each(std::execution::par_unseq,
        particles.begin(), particles.end(),
        [this, deltaTime, gravityModifier](Particle& p) {
            p.currentTime += deltaTime;
            if (p.currentTime < p.lifeTime) {
                UpdateSingleParticle(p, deltaTime, gravityModifier);
            }
        });

    // Phase 2: 寿命切れパーティクルをまとめて削除（シングルスレッド）
    const auto newEnd = std::remove_if(particles.begin(), particles.end(),
        [](const Particle& p) { return p.currentTime >= p.lifeTime; });

    const uint32_t destroyedCount =
        static_cast<uint32_t>(std::distance(newEnd, particles.end()));

    particles.erase(newEnd, particles.end());
    return destroyedCount;
}

void ParticleUpdater::UpdateSingleParticle(Particle& particle, float deltaTime, float gravityModifier) {
    // 力の適用（MainModuleのgravityModifierを考慮）
    if (forceModule_ && forceModule_->IsEnabled()) {
        forceModule_->ApplyForces(particle, deltaTime, gravityModifier);
    }

    // 位置の更新
    particle.transform.translate.x += particle.velocity.x * deltaTime;
    particle.transform.translate.y += particle.velocity.y * deltaTime;
    particle.transform.translate.z += particle.velocity.z * deltaTime;

    // 色の更新（initialColorからのグラデーション）
    if (colorModule_ && colorModule_->IsEnabled()) {
        colorModule_->UpdateColor(particle);
    }

    // サイズの更新
    if (sizeModule_ && sizeModule_->IsEnabled()) {
        sizeModule_->UpdateSize(particle);
    }

    // 回転の更新
    if (rotationModule_ && rotationModule_->IsEnabled()) {
        rotationModule_->UpdateRotation(particle, deltaTime);
    }

    // ノイズの適用
    if (noiseModule_ && noiseModule_->IsEnabled()) {
        noiseModule_->ApplyNoise(particle, deltaTime);
    }
}
}
