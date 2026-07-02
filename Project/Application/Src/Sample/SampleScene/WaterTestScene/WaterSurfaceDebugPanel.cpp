#include "pch.h"
#include "WaterSurfaceDebugPanel.h"

#ifdef USE_IMGUI

#include "WaterSurfaceRuntimeController.h"

#include "Utility/Debug/ImGui/ImGuiAll.h"

#include <algorithm>

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
	"FFT Jacobian",
};
}

void WaterSurfaceDebugPanel::Initialize(WaterSurfaceRuntimeController& runtimeController) {
	if (WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane()) {
		// デバッグパネルの既定状態を水面へ反映する
		waterPlane->SetDepthFadeDebug(depthFadeDebugEnabled_, depthFadeDebugScale_);
		waterPlane->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(depthDebugViewMode_));
	}
}

void WaterSurfaceDebugPanel::Draw(WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane || !ImGui::CollapsingHeader("デバッグ / 診断", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	ImGui::TextDisabled("ここは見た目調整ではなく、状態確認と可視化のための項目です。");
	DrawCommonDebugSection(runtimeController);
	DrawFFTOceanDebugSection(runtimeController, editorFacade);
	DrawGerstnerWaveDebugSection(runtimeController);
	DrawCausticsDebugSection(editorFacade);
}

void WaterSurfaceDebugPanel::DrawCommonDebugSection(WaterSurfaceRuntimeController& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane || !ImGui::TreeNodeEx("共通デバッグ", ImGuiTreeNodeFlags_DefaultOpen)) {
		return;
	}

	const WaterConstants& waterConstants = waterPlane->GetWaterConstants();
	const WaterFrameConstants& frameConstants = waterPlane->GetFrameConstants();

	ImGui::Text("現在の描画方式: %s", waterPlane->IsUsingFFTOcean() ? "FFTOcean" : "Gerstner Wave");
	ImGui::Text("反射が有効: %s", frameConstants.reflectionEnabled != 0 ? "はい" : "いいえ");
	ImGui::Text("水面高さ: %.3f", waterPlane->GetTransform().translate.y);
	ImGui::Text("波時間: %.3f", waterConstants.time);
	ImGui::Text("有効波数: %u", waterConstants.activeWaveCount);

	ImGui::SeparatorText("Depth Fade デバッグ");
	bool debugChanged = false;
	debugChanged |= ImGui::Checkbox("Depth Fade デバッグを有効にする", &depthFadeDebugEnabled_);
	debugChanged |= ImGui::SliderFloat("デバッグ表示倍率", &depthFadeDebugScale_, 0.1f, 8.0f, "%.2f");
	debugChanged |= ImGui::Combo("可視化モード", &depthDebugViewMode_, kWaterDebugViewNames, IM_ARRAYSIZE(kWaterDebugViewNames));
	if (debugChanged) {
		waterPlane->SetDepthFadeDebug(depthFadeDebugEnabled_, depthFadeDebugScale_);
		waterPlane->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(depthDebugViewMode_));
	}

	ImGui::TreePop();
}

void WaterSurfaceDebugPanel::DrawFFTOceanDebugSection(
	WaterSurfaceRuntimeController& runtimeController,
	WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	const ImGuiTreeNodeFlags flags = waterPlane->IsUsingFFTOcean() ? ImGuiTreeNodeFlags_DefaultOpen : 0;
	if (!ImGui::TreeNodeEx(waterPlane->IsUsingFFTOcean() ? "FFTOcean デバッグ" : "FFTOcean デバッグ（待機中）", flags)) {
		return;
	}

	const WaterEditorFFTSettings settings = editorFacade.GetFFTSettings();

	ImGui::Text("状態: %s", waterPlane->IsUsingFFTOcean() ? "使用中" : "待機中");
	ImGui::Text("FFT SRV 接続: %s", waterPlane->HasFFTOceanTextureSRVs() ? "有効" : "無効");

	if (!settings.managerAvailable) {
		ImGui::TextDisabled("FFTOceanManager を取得できません。");
		ImGui::TreePop();
		return;
	}

	ImGui::Text("初期化状態: %s", settings.initialized ? "完了" : "未初期化");
	ImGui::Text("解像度: %d", settings.resolution);
	ImGui::Text("パッチ長: %.2f", settings.patchLength);
	ImGui::Text("振幅スケール: %.3f", settings.amplitudeScale);
	ImGui::Text("風向: (%.3f, %.3f)", settings.windDirection[0], settings.windDirection[1]);
	ImGui::Text("風速: %.3f", settings.windSpeed);
	ImGui::Text("Choppiness: %.3f", settings.choppiness);
	ImGui::Text("成分数: %u", settings.activeComponentCount);
	ImGui::Text("重力: %.3f", settings.gravity);
	ImGui::Separator();
	ImGui::TextDisabled("注: DXR 屈折データは現在 Gerstner 系 WaterConstants を参照しており、FFT 表示と完全一致しない場合があります。");

	ImGui::TreePop();
}

void WaterSurfaceDebugPanel::DrawGerstnerWaveDebugSection(WaterSurfaceRuntimeController& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	const bool isActive = !waterPlane->IsUsingFFTOcean();
	const ImGuiTreeNodeFlags flags = isActive ? ImGuiTreeNodeFlags_DefaultOpen : 0;
	if (!ImGui::TreeNodeEx(isActive ? "Gerstner Wave デバッグ" : "Gerstner Wave デバッグ（待機中）", flags)) {
		return;
	}

	const WaterConstants& waterConstants = waterPlane->GetWaterConstants();
	const WaveParams* waves = waterPlane->GetWaves();
	const uint32_t activeWaveCount = std::min(waterConstants.activeWaveCount, kMaxWaterWaveCount);

	ImGui::Text("状態: %s", isActive ? "使用中" : "待機中");
	ImGui::Text("有効波数: %u / %u", activeWaveCount, kMaxWaterWaveCount);

	float maxAmplitude = 0.0f;
	float minWavelength = activeWaveCount > 0 ? waves[0].wavelength : 0.0f;
	float maxWavelength = 0.0f;
	float averageSpeed = 0.0f;
	for (uint32_t waveIndex = 0; waveIndex < activeWaveCount; ++waveIndex) {
		maxAmplitude = std::max(maxAmplitude, waves[waveIndex].amplitude);
		minWavelength = std::min(minWavelength, waves[waveIndex].wavelength);
		maxWavelength = std::max(maxWavelength, waves[waveIndex].wavelength);
		averageSpeed += waves[waveIndex].speed;
	}
	if (activeWaveCount > 0) {
		averageSpeed /= static_cast<float>(activeWaveCount);
	}

	ImGui::Text("最大振幅: %.3f", maxAmplitude);
	ImGui::Text("波長レンジ: %.3f - %.3f", minWavelength, maxWavelength);
	ImGui::Text("平均速度: %.3f", averageSpeed);

	if (ImGui::TreeNode("有効波プレビュー")) {
		for (uint32_t waveIndex = 0; waveIndex < activeWaveCount; ++waveIndex) {
			ImGui::BulletText(
				"波 %u: dir=(%.2f, %.2f) amp=%.3f len=%.3f speed=%.3f steep=%.3f",
				waveIndex,
				waves[waveIndex].direction.x,
				waves[waveIndex].direction.y,
				waves[waveIndex].amplitude,
				waves[waveIndex].wavelength,
				waves[waveIndex].speed,
				waves[waveIndex].steepness);
		}
		ImGui::TreePop();
	}

	ImGui::TreePop();
}

void WaterSurfaceDebugPanel::DrawCausticsDebugSection(WaterEditorFacade& editorFacade) {
	if (!ImGui::TreeNode("コースティクス デバッグ")) {
		return;
	}

	WaterEditorCausticsSettings settings = editorFacade.GetCausticsSettings();
	if (!settings.techniqueAvailable) {
		ImGui::TextDisabled("WaterCausticsTechnique が登録されていません。");
		ImGui::TreePop();
		return;
	}

	bool changed = false;

	// コースティクス計算に影響する主要パラメータを調整する
	changed |= ImGui::SliderFloat("強度", &settings.intensity, 0.0f, 8.0f, "%.3f");
	changed |= ImGui::SliderFloat("深度減衰", &settings.depthAttenuation, 0.0f, 4.0f, "%.3f");
	changed |= ImGui::SliderFloat("曲率スケール", &settings.curvatureScale, 0.0f, 30.0f, "%.3f");
	changed |= ImGui::SliderFloat("水面サンプル半径", &settings.surfaceSampleRadius, 0.05f, 2.0f, "%.3f");
	changed |= ImGui::SliderFloat("屈折率", &settings.refractiveIndex, 1.0f, 1.6f, "%.4f");
	changed |= ImGui::SliderFloat("受光面法線強度", &settings.receiverNormalStrength, 0.0f, 2.0f, "%.3f");
	changed |= ImGui::SliderFloat("フォーカス強度", &settings.alignmentPower, 1.0f, 64.0f, "%.3f");

	// デバッグ描画モードとログ出力の切り替えを提供する
	ImGui::SeparatorText("デバッグ表示");
	changed |= ImGui::SliderFloat("表示倍率", &settings.debugDisplayScale, 0.1f, 16.0f, "%.2f");
	static const char* kCausticsDebugModes[] = {
		"合成結果",
		"Raw RGB",
		"グレースケール",
	};
	changed |= ImGui::Combo("表示モード", &settings.debugViewMode, kCausticsDebugModes, IM_ARRAYSIZE(kCausticsDebugModes));
	changed |= ImGui::Checkbox("ログ出力を有効にする", &settings.debugLogEnabled);

	// 現在の出力先や入力 SRV を診断情報として表示する
	const WaterEditorCausticsDiagnostics diagnostics = editorFacade.GetCausticsDiagnostics();
	ImGui::Text("波数: %u", diagnostics.activeWaveCount);
	ImGui::Text("メインライト: %s", diagnostics.mainLightEnabled ? "有効" : "無効");
	ImGui::Text("出力 SRV: 0x%llX", diagnostics.outputHandle);
	ImGui::Text("WorldPos SRV: 0x%llX", diagnostics.worldPositionHandle);
	ImGui::Text("Normal SRV: 0x%llX", diagnostics.normalHandle);

	if (changed) {
		editorFacade.ApplyCausticsSettings(settings);
	}

	ImGui::TreePop();
}

#endif
