#include "ParticleUpdater.h"
#include "Engine/Particle/ParticleSystem.h" // Particle構造体のため
#include "Engine/Particle/Modules/ForceModule.h"
#include "Engine/Particle/Modules/ColorModule.h"
#include "Engine/Particle/Modules/SizeModule.h"
#include "Engine/Particle/Modules/RotationModule.h"
#include "Engine/Particle/Modules/NoiseModule.h"


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
	std::list<Particle>& particles,
	float deltaTime,
	float gravityModifier
) {
	uint32_t destroyedCount = 0;

	for (auto particleIterator = particles.begin(); particleIterator != particles.end();) {
		// ライフタイムチェック
		particleIterator->currentTime += deltaTime;
		if (particleIterator->currentTime >= particleIterator->lifeTime) {
			particleIterator = particles.erase(particleIterator);
			++destroyedCount;
			continue;
		}

		// 単一パーティクルの更新
		UpdateSingleParticle(*particleIterator, deltaTime, gravityModifier);

		++particleIterator;
	}

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
