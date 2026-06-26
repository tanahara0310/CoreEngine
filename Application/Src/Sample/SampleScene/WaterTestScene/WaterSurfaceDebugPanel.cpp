#include "pch.h"
#include "WaterSurfaceDebugPanel.h"

#ifdef USE_IMGUI

#include "WaterSurfaceRuntimeController.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Utility/Debug/ImGui/ImGuiAll.h"

using namespace CoreEngine;

namespace {
// Water.PS.hlsl のデバッグ表示モード名を ImGui へそのまま提示する。
const char* const kWaterDebugViewNames[] = {
	"なし",
	"生の深度",
	"線形深度",
	"深度差",
	"スクリーンUV",
	"シーンカラー",
	"反射",
	"フレネル",
	"RT屈折",
	"RT屈折理由",
	"RT屈折とシーン比較",
	"透過光",
	"吸収",
	"反射率",
	"最終合成",
	"RT屈折成功マスク",
};
}

void WaterSurfaceDebugPanel::Initialize(WaterSurfaceRuntimeController& runtimeController) {
	if (WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane()) {
		// デバッグパネルの既定状態を水面へ反映する
		waterPlane->SetDepthFadeDebug(depthFadeDebugEnabled_, depthFadeDebugScale_);
		waterPlane->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(depthDebugViewMode_));
	}
}

void WaterSurfaceDebugPanel::Draw(WaterSurfaceRuntimeController& runtimeController, EngineSystem& engine) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane || !ImGui::CollapsingHeader("デバッグ / 診断", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	// 現在の水面状態を診断用に表示する
	const WaterConstants& waterConstants = waterPlane->GetWaterConstants();
	const WaterFrameConstants& frameConstants = waterPlane->GetFrameConstants();

	ImGui::TextDisabled("ここは見た目調整ではなく、状態確認と可視化のための項目です。");
	ImGui::SeparatorText("診断情報");
	ImGui::Text("反射が有効: %s", frameConstants.reflectionEnabled != 0 ? "はい" : "いいえ");
	ImGui::Text("水面高さ: %.3f", waterPlane->GetTransform().translate.y);
	ImGui::Text("波時間: %.3f", waterConstants.time);
	ImGui::Text("有効波数: %u", waterConstants.activeWaveCount);

	// Depth Fade の可視化パラメータを直接切り替えられるようにする
	ImGui::Spacing();
	ImGui::SeparatorText("Depth Fade デバッグ");
	bool debugChanged = false;
	debugChanged |= ImGui::Checkbox("Depth Fade デバッグを有効にする", &depthFadeDebugEnabled_);
	debugChanged |= ImGui::SliderFloat("デバッグ表示倍率", &depthFadeDebugScale_, 0.1f, 8.0f, "%.2f");
	debugChanged |= ImGui::Combo("可視化モード", &depthDebugViewMode_, kWaterDebugViewNames, IM_ARRAYSIZE(kWaterDebugViewNames));
	if (debugChanged) {
		waterPlane->SetDepthFadeDebug(depthFadeDebugEnabled_, depthFadeDebugScale_);
		waterPlane->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(depthDebugViewMode_));
	}

	DrawCausticsDebugSection(engine);
}

void WaterSurfaceDebugPanel::DrawCausticsDebugSection(EngineSystem& engine) {
	if (!ImGui::TreeNode("コースティクス デバッグ")) {
		return;
	}

	// WaterCausticsTechnique が存在する場合のみ詳細デバッグ UI を表示する
	if (auto* techniqueManager = engine.GetComponent<RenderingTechniqueManager>()) {
		if (auto* waterCaustics = techniqueManager->GetTechnique<WaterCausticsTechnique>(RenderingTechniqueNames::WaterCaustics)) {
			WaterCausticsTechnique::Params params = waterCaustics->GetParams();
			bool changed = false;

			// コースティクス計算に影響する主要パラメータを調整する
			changed |= ImGui::SliderFloat("強度", &params.intensity, 0.0f, 8.0f, "%.3f");
			changed |= ImGui::SliderFloat("深度減衰", &params.depthAttenuation, 0.0f, 4.0f, "%.3f");
			changed |= ImGui::SliderFloat("曲率スケール", &params.curvatureScale, 0.0f, 30.0f, "%.3f");
			changed |= ImGui::SliderFloat("水面サンプル半径", &params.surfaceSampleRadius, 0.05f, 2.0f, "%.3f");
			changed |= ImGui::SliderFloat("屈折率", &params.refractiveIndex, 1.0f, 1.6f, "%.4f");
			changed |= ImGui::SliderFloat("受光面法線強度", &params.receiverNormalStrength, 0.0f, 2.0f, "%.3f");
			changed |= ImGui::SliderFloat("フォーカス強度", &params.alignmentPower, 1.0f, 64.0f, "%.3f");

			// デバッグ描画モードとログ出力の切り替えを提供する
			ImGui::SeparatorText("デバッグ表示");
			changed |= ImGui::SliderFloat("表示倍率", &params.debugDisplayScale, 0.1f, 16.0f, "%.2f");
			int debugViewMode = static_cast<int>(params.debugViewMode);
			static const char* kCausticsDebugModes[] = {
				"合成結果",
				"Raw RGB",
				"グレースケール",
			};
			if (ImGui::Combo("表示モード", &debugViewMode, kCausticsDebugModes, IM_ARRAYSIZE(kCausticsDebugModes))) {
				params.debugViewMode = static_cast<uint32_t>(debugViewMode);
				changed = true;
			}

			bool debugLogEnabled = (params.debugLogEnabled != 0);
			if (ImGui::Checkbox("ログ出力を有効にする", &debugLogEnabled)) {
				params.debugLogEnabled = debugLogEnabled ? 1u : 0u;
				changed = true;
			}

			// 現在の出力先や入力 SRV を診断情報として表示する
			const auto& diagnostics = waterCaustics->GetDiagnostics();
			ImGui::Text("波数: %u", diagnostics.activeWaveCount);
			ImGui::Text("メインライト: %s", diagnostics.mainLightEnabled != 0 ? "有効" : "無効");
			ImGui::Text("出力 SRV: 0x%llX", diagnostics.outputHandle);
			ImGui::Text("WorldPos SRV: 0x%llX", diagnostics.worldPositionHandle);
			ImGui::Text("Normal SRV: 0x%llX", diagnostics.normalHandle);

			if (changed) {
				waterCaustics->SetParams(params);
			}
		} else {
			ImGui::TextDisabled("WaterCausticsTechnique が登録されていません。");
		}
	} else {
		ImGui::TextDisabled("RenderingTechniqueManager を取得できません。");
	}

	ImGui::TreePop();
}

#endif
