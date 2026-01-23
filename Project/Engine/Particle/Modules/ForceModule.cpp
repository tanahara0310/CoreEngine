#include "ForceModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要
#include <algorithm>

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
ForceModule::ForceModule() {
	forceData_.gravity = { 0.0f, -9.8f, 0.0f };
	forceData_.wind = { 0.0f, 0.0f, 0.0f };
	forceData_.drag = 0.0f;
	forceData_.useAccelerationField = false;
  forceData_.acceleration = { 0.0f, 0.0f, 0.0f };
 forceData_.area = BoundingBox();
}

void ForceModule::ApplyForces(Particle& particle, float deltaTime, float gravityModifier) {
	if (!enabled_) {
		return;
	}

	// 重力を適用（gravityModifierを考慮）
	particle.velocity.x += forceData_.gravity.x * gravityModifier * deltaTime;
	particle.velocity.y += forceData_.gravity.y * gravityModifier * deltaTime;
	particle.velocity.z += forceData_.gravity.z * gravityModifier * deltaTime;

	// 風力を適用
	particle.velocity.x += forceData_.wind.x * deltaTime;
	particle.velocity.y += forceData_.wind.y * deltaTime;
	particle.velocity.z += forceData_.wind.z * deltaTime;

	// 抵抗力を適用
	if (forceData_.drag > 0.0f) {
		float dragFactor = 1.0f - (forceData_.drag * deltaTime);
		dragFactor = (std::max)(0.0f, dragFactor); // 負の値にならないように

		particle.velocity.x *= dragFactor;
		particle.velocity.y *= dragFactor;
		particle.velocity.z *= dragFactor;
	}

	// 加速度フィールドを適用
	if (forceData_.useAccelerationField) {
		if (CollisionUtils::IsColliding(particle.transform.translate, forceData_.area)) {
			particle.velocity.x += forceData_.acceleration.x * deltaTime;
			particle.velocity.y += forceData_.acceleration.y * deltaTime;
			particle.velocity.z += forceData_.acceleration.z * deltaTime;
		}
	}
}

#ifdef _DEBUG
bool ForceModule::ShowImGui() {
	bool changed = false;
	
	// 有効/無効の切り替え
	if (ImGui::Checkbox("有効##力場", &enabled_)) {
		changed = true;
	}

	if (!enabled_) {
		ImGui::BeginDisabled();
	}

	changed |= ImGui::DragFloat3("重力", &forceData_.gravity.x, 0.1f);
	changed |= ImGui::DragFloat3("風", &forceData_.wind.x, 0.1f);
	changed |= ImGui::DragFloat("抵抗", &forceData_.drag, 0.01f, 0.0f, 1.0f);
	
	ImGui::Separator();
	changed |= ImGui::Checkbox("加速フィールド使用", &forceData_.useAccelerationField);
	
	if (forceData_.useAccelerationField) {
		changed |= ImGui::DragFloat3("加速度", &forceData_.acceleration.x, 0.1f);
		changed |= ImGui::DragFloat3("エリア最小", &forceData_.area.min.x, 0.1f);
		changed |= ImGui::DragFloat3("エリア最大", &forceData_.area.max.x, 0.1f);
	}

	if (!enabled_) {
		ImGui::EndDisabled();
	}

	return changed;
}
#endif
}
