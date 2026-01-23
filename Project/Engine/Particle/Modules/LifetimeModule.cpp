#include "LifetimeModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要
#include <algorithm>


namespace CoreEngine
{
void LifetimeModule::ApplyInitialLifetime(Particle& particle) {
	if (!enabled_) {
		particle.lifeTime = lifetimeData_.startLifetime;
		particle.currentTime = 0.0f;
		return;
	}

	float lifetime = lifetimeData_.startLifetime;

	// ランダム性を適用
	if (lifetimeData_.lifetimeRandomness > 0.0f) {
		float randomFactor = 1.0f + random_.GetFloat(-lifetimeData_.lifetimeRandomness, lifetimeData_.lifetimeRandomness);
		lifetime *= (std::max)(0.1f, randomFactor); // 最小0.1秒は保証
	}

	particle.lifeTime = lifetime;
	particle.currentTime = 0.0f;
}

bool LifetimeModule::UpdateLifetime(Particle& particle, float deltaTime) {
	if (!enabled_) {
		return true; // 無効の場合は常に生存
	}

	particle.currentTime += deltaTime;
	return particle.currentTime < particle.lifeTime;
}

#ifdef _DEBUG
bool LifetimeModule::ShowImGui() {
	bool changed = false;
	
	// 有効/無効の切り替え
	if (ImGui::Checkbox("有効##寿命", &enabled_)) {
		changed = true;
	}

	if (!enabled_) {
		ImGui::BeginDisabled();
	}

	changed |= ImGui::DragFloat("初期寿命", &lifetimeData_.startLifetime, 0.1f, 0.1f, 60.0f);
	changed |= ImGui::DragFloat("寿命ランダム性", &lifetimeData_.lifetimeRandomness, 0.01f, 0.0f, 1.0f);

	if (!enabled_) {
		ImGui::EndDisabled();
	}

	return changed;
}
#endif
}
