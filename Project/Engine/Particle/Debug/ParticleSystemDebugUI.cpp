#include "ParticleSystemDebugUI.h"

#ifdef _DEBUG

#include "Engine/Particle/ParticleSystem.h"
#include "Engine/Graphics/Model/ModelResource.h"
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"


namespace CoreEngine
{
bool ParticleSystemDebugUI::ShowImGui(ParticleSystem* particleSystem) {
	if (!particleSystem) return false;

	bool changed = false;

	// システム状態表示
	ShowSystemStatus(particleSystem);

	// 制御ボタン
	ShowControlButtons(particleSystem);

	ImGui::Separator();

	// プリセット管理
	ShowPresetManager(particleSystem);

	// 各モジュールのUI
	ShowModules(particleSystem);

	// エミッター設定
	ShowEmitterSettings(particleSystem);

	// 統計情報
	ShowStatistics(particleSystem);

	return changed;
}

void ParticleSystemDebugUI::ShowSystemStatus(ParticleSystem* particleSystem) {
	ImGui::Text("=== パーティクルシステム ===");

	uint32_t currentCount = particleSystem->GetParticleCount();
	uint32_t maxParticles = particleSystem->GetMaxParticleCount();
	float usageRatio = static_cast<float>(currentCount) / static_cast<float>(maxParticles);

	ImGui::Text("状態: %s | パーティクル数: %u/%u (%.0f%%)",
		particleSystem->IsPlaying() ? "動作中" : "停止中",
		currentCount,
		maxParticles,
		usageRatio * 100.0f);

	if (usageRatio > 0.8f) {
		ImGui::SameLine();
		if (usageRatio > 0.95f) {
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[危険]");
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[警告]");
		}
	}
}

void ParticleSystemDebugUI::ShowControlButtons(ParticleSystem* particleSystem) {
	if (ImGui::Button("再生")) { particleSystem->Play(); }
	ImGui::SameLine();
	if (ImGui::Button("停止")) { particleSystem->Stop(); }
	ImGui::SameLine();
	if (ImGui::Button("クリア")) { particleSystem->Clear(); }
}

void ParticleSystemDebugUI::ShowPresetManager(ParticleSystem* particleSystem) {
	// プリセットマネージャーのUIを表示
	if (particleSystem->presetManager_) {
		particleSystem->presetManager_->ShowImGui(particleSystem);
	}
}

void ParticleSystemDebugUI::ShowModules(ParticleSystem* particleSystem) {
	if (ImGui::CollapsingHeader("メインモジュール")) {
		particleSystem->GetMainModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("放出モジュール")) {
		particleSystem->GetEmissionModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("形状モジュール")) {
		particleSystem->GetShapeModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("速度モジュール")) {
		particleSystem->GetVelocityModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("色モジュール")) {
		particleSystem->GetColorModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("力場モジュール")) {
		particleSystem->GetForceModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("サイズモジュール")) {
		particleSystem->GetSizeModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("回転モジュール")) {
		particleSystem->GetRotationModule().ShowImGui();
	}

	if (ImGui::CollapsingHeader("ノイズモジュール")) {
		particleSystem->GetNoiseModule().ShowImGui();
	}
}

void ParticleSystemDebugUI::ShowEmitterSettings(ParticleSystem* particleSystem) {
	if (!ImGui::CollapsingHeader("エミッター設定")) return;

	// エミッタートランスフォームの取得・設定
	Vector3 emitterPos = particleSystem->GetEmitterPosition();
	if (ImGui::DragFloat3("位置", &emitterPos.x, 0.01f)) {
		particleSystem->SetEmitterPosition(emitterPos);
	}

	// 描画モード選択
	ImGui::Separator();
	ImGui::Text("描画モード:");
	int currentRenderMode = static_cast<int>(particleSystem->GetRenderMode());
	const char* renderModeNames[] = { "ビルボード", "3Dモデル" };
	if (ImGui::Combo("モード", &currentRenderMode, renderModeNames, IM_ARRAYSIZE(renderModeNames))) {
		// 描画モード変更はSetModelResource()経由で行う必要があります
	}

	// モデルパーティクルの情報表示
	if (particleSystem->IsModelParticle()) {
		auto* modelResource = particleSystem->GetModelResource();
		if (modelResource) {
			ImGui::Text("モデル: %s", modelResource->GetFilePath().c_str());
			ImGui::Text("頂点数: %u", modelResource->GetVertexCount());
		} else {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "警告: モデルが設定されていません");
		}
	}

	ImGui::Separator();

	// ビルボードタイプ
	if (!particleSystem->IsModelParticle()) {
		static const char* billboardTypeNames[] = {
			"なし", "カメラ向き", "Y軸固定", "スクリーン平行"
		};
		int currentBillboardType = static_cast<int>(particleSystem->GetBillboardType());
		if (ImGui::Combo("ビルボードタイプ", &currentBillboardType, billboardTypeNames, IM_ARRAYSIZE(billboardTypeNames))) {
			particleSystem->SetBillboardType(static_cast<BillboardType>(currentBillboardType));
		}
	}

	// ブレンドモード
	static const char* blendModeNames[] = {
		"なし", "通常", "加算", "減算", "乗算", "スクリーン"
	};
	int currentBlendMode = static_cast<int>(particleSystem->GetBlendMode());
	if (ImGui::Combo("ブレンドモード", &currentBlendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {
		particleSystem->SetBlendMode(static_cast<BlendMode>(currentBlendMode));
	}
}

void ParticleSystemDebugUI::ShowStatistics(ParticleSystem* particleSystem) {
	if (!ImGui::CollapsingHeader("統計情報")) return;

	const auto& stats = particleSystem->GetStatistics();

	ImGui::Text("作成されたパーティクル数: %u", stats.totalParticlesCreated);
	ImGui::Text("破棄されたパーティクル数: %u", stats.totalParticlesDestroyed);
	ImGui::Text("最大同時パーティクル数: %u", stats.peakParticleCount);
	ImGui::Text("平均ライフタイム: %.2f秒", stats.averageLifetime);
	ImGui::Text("システム稼働時間: %.2f秒", stats.systemRuntime);

	if (ImGui::Button("統計リセット")) {
		particleSystem->ResetStatistics();
	}
}

#endif // _DEBUG
}
