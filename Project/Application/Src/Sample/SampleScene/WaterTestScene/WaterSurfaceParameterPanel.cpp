#include "pch.h"
#include "WaterSurfaceParameterPanel.h"

#ifdef USE_IMGUI

#include "WaterSurfaceRuntimeController.h"

#include "Math/MathCore.h"
#include "Utility/Debug/ImGui/ImGuiAll.h"
#include "Utility/Random/RandomGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

using namespace CoreEngine;

namespace {
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;

// 波方向のゼロ長入力を吸収しつつ正規化する。
Vector2 NormalizeDirection(const Vector2& direction) {
	const Vector3 direction3 = { direction.x, direction.y, 0.0f };
	if (MathCore::Vector::Length(direction3) <= 1.0e-5f) {
		return { 1.0f, 0.0f };
	}

	const Vector3 normalizedDirection = MathCore::Vector::Normalize(direction3);
	return { normalizedDirection.x, normalizedDirection.y };
}

// 主波向から指定角度だけ回転した正規化方向を返す。
Vector2 RotateDirection(const Vector2& direction, float radians) {
	const Matrix4x4 rotationMatrix = MathCore::Matrix::RotationZ(radians);
	const Vector3 rotatedDirection = MathCore::CoordinateTransform::TransformNormal(
		{ direction.x, direction.y, 0.0f },
		rotationMatrix);
	return NormalizeDirection({ rotatedDirection.x, rotatedDirection.y });
}

// UI 上の波数を WaterPlaneObject の有効範囲に丸める。
uint32_t ClampWaveCountToRange(int count) {
	return static_cast<uint32_t>(std::clamp(count, 1, static_cast<int>(kMaxWaterWaveCount)));
}

const char* const kPresetNames[] = {
	"湖（静かな水）",
	"海（波が立つ水）",
	"池・プール",
	"雨水・水たまり",
};
}

void WaterSurfaceParameterPanel::Initialize(WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade) {
	const WaterEditorFFTSettings fftSettings = editorFacade.GetFFTSettings();
	fftOceanParameters_.enabled = fftSettings.enabled;
	fftOceanParameters_.patchLength = fftSettings.patchLength;
	fftOceanParameters_.amplitudeScale = fftSettings.amplitudeScale;
	fftOceanParameters_.windDirection[0] = fftSettings.windDirection[0];
	fftOceanParameters_.windDirection[1] = fftSettings.windDirection[1];
	fftOceanParameters_.windSpeed = fftSettings.windSpeed;
	fftOceanParameters_.choppiness = fftSettings.choppiness;
	fftOceanParameters_.activeComponentCount = fftSettings.activeComponentCount;
	fftOceanParameters_.gravity = fftSettings.gravity;
	fftOceanParameters_.resolution = fftSettings.resolution;

	// 現在の DXR 屈折設定を UI の初期値へ反映する
	rtRefractionOffsetPixels_ = editorFacade.GetRayTracingSettings().maxRefractionOffsetPixels;

	// 既定プリセットを適用して UI と水面状態を同期する
	ApplyWaterPreset(runtimeController, static_cast<WaterPresetType>(waveToolState_.preset));
}

void WaterSurfaceParameterPanel::Draw(WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade) {
	// 通常パラメータと波編集ツールを用途別に分けて表示する
	if (ImGui::CollapsingHeader("パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawRuntimeParameterSection(runtimeController, editorFacade);
		DrawFFTOceanSection(runtimeController, editorFacade);
	}

	if (ImGui::CollapsingHeader("波生成 / 編集", ImGuiTreeNodeFlags_DefaultOpen)) {
		DrawWaveToolSection(runtimeController);
	}
	}

void WaterSurfaceParameterPanel::DrawFFTOceanSection(
	WaterSurfaceRuntimeController& runtimeController,
	WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	const WaterEditorFFTSettings currentFFTSettings = editorFacade.GetFFTSettings();
	fftOceanParameters_.resolution = currentFFTSettings.resolution;

	ImGui::Spacing();
	ImGui::SeparatorText("FFT Ocean");
	if (ImGui::Checkbox("FFT Ocean 描画を使用する", &fftOceanParameters_.enabled)) {
		WaterEditorFFTSettings updatedSettings = editorFacade.GetFFTSettings();
		updatedSettings.enabled = fftOceanParameters_.enabled;
		editorFacade.ApplyFFTSettings(updatedSettings);
	}

	if (!currentFFTSettings.managerAvailable) {
		ImGui::TextDisabled("FFTOceanManager を取得できません。");
		return;
	}

	ImGui::Text("解像度: %d (再生成が必要なため表示のみ)", fftOceanParameters_.resolution);
	bool fftChanged = false;
	bool fftEditCommitted = false;
	auto trackFFTEdit = [&](bool changed) {
		fftChanged |= changed;
		fftEditCommitted |= ImGui::IsItemDeactivatedAfterEdit();
	};

	trackFFTEdit(ImGui::SliderFloat("パッチ長", &fftOceanParameters_.patchLength, 8.0f, 512.0f, "%.2f"));
	trackFFTEdit(ImGui::SliderFloat("振幅スケール", &fftOceanParameters_.amplitudeScale, 0.0f, 4.0f, "%.3f"));
	trackFFTEdit(ImGui::DragFloat2("風向", fftOceanParameters_.windDirection, 0.01f, -1.0f, 1.0f, "%.3f"));
	trackFFTEdit(ImGui::SliderFloat("風速", &fftOceanParameters_.windSpeed, 0.0f, 64.0f, "%.2f"));
	trackFFTEdit(ImGui::SliderFloat("Choppiness", &fftOceanParameters_.choppiness, 0.0f, 4.0f, "%.3f"));
	trackFFTEdit(ImGui::SliderInt("スペクトル成分数", &fftOceanParameters_.activeComponentCount, 1, 64));
	trackFFTEdit(ImGui::SliderFloat("重力", &fftOceanParameters_.gravity, 0.1f, 20.0f, "%.3f"));

	fftEditCommitted = fftEditCommitted || (!ImGui::IsAnyItemActive() && fftChanged);
	if (fftEditCommitted) {
		WaterEditorFFTSettings updatedSettings = editorFacade.GetFFTSettings();
		updatedSettings.enabled = fftOceanParameters_.enabled;
		updatedSettings.patchLength = fftOceanParameters_.patchLength;
		updatedSettings.amplitudeScale = fftOceanParameters_.amplitudeScale;
		updatedSettings.windDirection[0] = fftOceanParameters_.windDirection[0];
		updatedSettings.windDirection[1] = fftOceanParameters_.windDirection[1];
		updatedSettings.windSpeed = fftOceanParameters_.windSpeed;
		updatedSettings.choppiness = fftOceanParameters_.choppiness;
		updatedSettings.activeComponentCount = fftOceanParameters_.activeComponentCount;
		updatedSettings.gravity = fftOceanParameters_.gravity;
		editorFacade.ApplyFFTSettings(updatedSettings);

		const WaterEditorFFTSettings appliedSettings = editorFacade.GetFFTSettings();
		fftOceanParameters_.patchLength = appliedSettings.patchLength;
		fftOceanParameters_.amplitudeScale = appliedSettings.amplitudeScale;
		fftOceanParameters_.windDirection[0] = appliedSettings.windDirection[0];
		fftOceanParameters_.windDirection[1] = appliedSettings.windDirection[1];
		fftOceanParameters_.windSpeed = appliedSettings.windSpeed;
		fftOceanParameters_.choppiness = appliedSettings.choppiness;
		fftOceanParameters_.activeComponentCount = appliedSettings.activeComponentCount;
		fftOceanParameters_.gravity = appliedSettings.gravity;
	}

	if (ImGui::Button("FFT Ocean 設定を既定値へ戻す", ImVec2(-1.0f, 0.0f))) {
		const WaterEditorFFTSettings appliedSettings = editorFacade.ResetFFTSettings();
		fftOceanParameters_.enabled = appliedSettings.enabled;
		fftOceanParameters_.patchLength = appliedSettings.patchLength;
		fftOceanParameters_.amplitudeScale = appliedSettings.amplitudeScale;
		fftOceanParameters_.windDirection[0] = appliedSettings.windDirection[0];
		fftOceanParameters_.windDirection[1] = appliedSettings.windDirection[1];
		fftOceanParameters_.windSpeed = appliedSettings.windSpeed;
		fftOceanParameters_.choppiness = appliedSettings.choppiness;
		fftOceanParameters_.activeComponentCount = static_cast<int>(appliedSettings.activeComponentCount);
		fftOceanParameters_.gravity = appliedSettings.gravity;
		fftOceanParameters_.resolution = static_cast<int>(appliedSettings.resolution);
	}
}

void WaterSurfaceParameterPanel::ApplyWaterPreset(WaterSurfaceRuntimeController& runtimeController, WaterPresetType preset) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	// プリセット値を UI キャッシュへ取り込み、現在選択中の種類を更新する
	const WaterPresetData& presetData = GetWaterPresetData(preset);
	waveToolState_.preset = static_cast<int>(preset);

	appearanceParameters_.baseColor[0] = presetData.baseColor.x;
	appearanceParameters_.baseColor[1] = presetData.baseColor.y;
	appearanceParameters_.baseColor[2] = presetData.baseColor.z;
	appearanceParameters_.baseColor[3] = presetData.baseColor.w;
	appearanceParameters_.roughness = presetData.roughness;
	appearanceParameters_.metallic = presetData.metallic;
	appearanceParameters_.fresnelReflectanceScale = presetData.fresnelReflectanceScale;
	appearanceParameters_.fresnelBaseReflectance = presetData.fresnelBaseReflectance;

	surfaceParameters_.absorptionCoeff = presetData.absorptionCoeff;
	surfaceParameters_.shallowColor[0] = presetData.shallowColor.x;
	surfaceParameters_.shallowColor[1] = presetData.shallowColor.y;
	surfaceParameters_.shallowColor[2] = presetData.shallowColor.z;
	surfaceParameters_.deepColor[0] = presetData.deepColor.x;
	surfaceParameters_.deepColor[1] = presetData.deepColor.y;
	surfaceParameters_.deepColor[2] = presetData.deepColor.z;
	surfaceParameters_.scrollSpeed[0] = presetData.scrollSpeed.x;
	surfaceParameters_.scrollSpeed[1] = presetData.scrollSpeed.y;
	surfaceParameters_.uvTiling[0] = presetData.uvTiling.x;
	surfaceParameters_.uvTiling[1] = presetData.uvTiling.y;

	// 見た目・透過・UV の設定を実際の水面オブジェクトへ反映する
	waterPlane->SetBaseColor({
		appearanceParameters_.baseColor[0],
		appearanceParameters_.baseColor[1],
		appearanceParameters_.baseColor[2],
		appearanceParameters_.baseColor[3] });
	waterPlane->SetRoughness(appearanceParameters_.roughness);
	waterPlane->SetMetallic(appearanceParameters_.metallic);
	waterPlane->SetIBLEnabled(appearanceParameters_.iblEnabled);
	waterPlane->SetFresnelParameters(
		appearanceParameters_.fresnelReflectanceScale,
		appearanceParameters_.fresnelBaseReflectance);
	waterPlane->SetDepthFade(surfaceParameters_.absorptionCoeff, surfaceParameters_.depthFadeEnabled);
	waterPlane->SetWaterColors(
		{ surfaceParameters_.shallowColor[0], surfaceParameters_.shallowColor[1], surfaceParameters_.shallowColor[2] },
		{ surfaceParameters_.deepColor[0], surfaceParameters_.deepColor[1], surfaceParameters_.deepColor[2] });
	waterPlane->SetScrollSpeed({ surfaceParameters_.scrollSpeed[0], surfaceParameters_.scrollSpeed[1] });
	waterPlane->SetUVTiling({ surfaceParameters_.uvTiling[0], surfaceParameters_.uvTiling[1] });

	// 必要に応じて推奨波数へ戻し、プリセットに応じた波を再生成する
	if (waveToolState_.lockRecommendedWaveCount || waveToolState_.autoRestoreRecommendedWaveCount) {
		RestoreRecommendedWaveCount(runtimeController, preset);
	}

	RegenerateLayeredWaves(runtimeController, preset, ClampWaveCountToRange(waveToolState_.activeWaveCount));
}

void WaterSurfaceParameterPanel::RestoreRecommendedWaveCount(WaterSurfaceRuntimeController& runtimeController, WaterPresetType preset) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	const WaterPresetData& presetData = GetWaterPresetData(preset);
	waveToolState_.activeWaveCount = static_cast<int>(presetData.recommendedWaveCount);
	// UI と水面オブジェクトの有効波数を推奨値へ同期する
	waterPlane->SetActiveWaveCount(presetData.recommendedWaveCount);
}

void WaterSurfaceParameterPanel::RegenerateLayeredWaves(
	WaterSurfaceRuntimeController& runtimeController,
	WaterPresetType preset,
	uint32_t activeWaveCount) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	const WaterPresetData& presetData = GetWaterPresetData(preset);
	auto& random = RandomGenerator::GetInstance();

	// プリセット定義の各レイヤー本数を、現在の有効波数に収まるよう配分する
	uint32_t layerCounts[kWaterWaveLayerCount] = {};
	uint32_t assignedCount = 0;
	for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount; ++layerIndex) {
		layerCounts[layerIndex] = std::min(presetData.layers[layerIndex].count, activeWaveCount - assignedCount);
		assignedCount += layerCounts[layerIndex];
		if (assignedCount >= activeWaveCount) {
			break;
		}
	}

	// 指定波数に満たない分はレイヤーへ均等気味に追加する
	uint32_t fillLayer = 0;
	while (assignedCount < activeWaveCount) {
		++layerCounts[fillLayer % kWaterWaveLayerCount];
		++assignedCount;
		++fillLayer;
	}

	// 主波向・角度ばらつき・各レイヤーの振幅範囲から Gerstner Wave を生成する
	const Vector2 dominantDirection = NormalizeDirection(presetData.dominantDirection);
	uint32_t waveIndex = 0;
	for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount && waveIndex < activeWaveCount; ++layerIndex) {
		const WaterWaveLayerConfig& layer = presetData.layers[layerIndex];
		const uint32_t countInLayer = layerCounts[layerIndex];
		for (uint32_t layerWaveIndex = 0; layerWaveIndex < countInLayer && waveIndex < activeWaveCount; ++layerWaveIndex, ++waveIndex) {
			const float baseAngle = (countInLayer > 1)
				? (-0.5f + static_cast<float>(layerWaveIndex) / static_cast<float>(countInLayer - 1)) * layer.directionSpreadRadians
				: 0.0f;
			const float jitter = random.GetFloat(-presetData.directionJitterRadians, presetData.directionJitterRadians);
			const Vector2 direction = RotateDirection(dominantDirection, baseAngle + jitter);

			WaveParams wave{};
			wave.direction = direction;
			wave.amplitude = random.GetFloat(layer.amplitudeMin, layer.amplitudeMax);
			wave.wavelength = random.GetFloat(layer.wavelengthMin, layer.wavelengthMax);
			wave.speed = random.GetFloat(layer.speedMin, layer.speedMax);
			wave.steepness = random.GetFloat(layer.steepnessMin, layer.steepnessMax);
			wave.phaseOffset = random.GetFloat(0.0f, kTwoPi);
			waterPlane->SetWave(waveIndex, wave);
		}
	}

	// 未使用スロットは主波向だけ残したゼロ初期値へ戻す
	for (; waveIndex < kMaxWaterWaveCount; ++waveIndex) {
		WaveParams wave{};
		wave.direction = dominantDirection;
		waterPlane->SetWave(waveIndex, wave);
	}

	waterPlane->SetActiveWaveCount(activeWaveCount);
}

void WaterSurfaceParameterPanel::DrawRuntimeParameterSection(
	WaterSurfaceRuntimeController& runtimeController,
	WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	// プリセット操作と見た目調整 UI を描画し、その場で水面へ反映する
	ImGui::SeparatorText("プリセット");
	const int previousPreset = waveToolState_.preset;
	ImGui::Combo("水面プリセット", &waveToolState_.preset, kPresetNames, IM_ARRAYSIZE(kPresetNames));
	if (waveToolState_.preset != previousPreset) {
		ApplyWaterPreset(runtimeController, static_cast<WaterPresetType>(waveToolState_.preset));
	}

	const WaterPresetType currentPreset = static_cast<WaterPresetType>(waveToolState_.preset);
	if (ImGui::Button("プリセットを再適用", ImVec2(-1.0f, 0.0f))) {
		ApplyWaterPreset(runtimeController, currentPreset);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("水面の見た目");
	if (ImGui::ColorEdit4("ベースカラー", appearanceParameters_.baseColor)) {
		waterPlane->SetBaseColor({
			appearanceParameters_.baseColor[0],
			appearanceParameters_.baseColor[1],
			appearanceParameters_.baseColor[2],
			appearanceParameters_.baseColor[3] });
	}
	if (ImGui::SliderFloat("ラフネス", &appearanceParameters_.roughness, 0.0f, 1.0f)) {
		waterPlane->SetRoughness(appearanceParameters_.roughness);
	}
	if (ImGui::SliderFloat("メタリック", &appearanceParameters_.metallic, 0.0f, 1.0f)) {
		waterPlane->SetMetallic(appearanceParameters_.metallic);
	}
	if (ImGui::Checkbox("IBLを有効にする", &appearanceParameters_.iblEnabled)) {
		waterPlane->SetIBLEnabled(appearanceParameters_.iblEnabled);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("反射 / 屈折");
	bool fresnelChanged = false;
	fresnelChanged |= ImGui::SliderFloat("フレネル反射スケール", &appearanceParameters_.fresnelReflectanceScale, 0.0f, 2.0f, "%.3f");
	fresnelChanged |= ImGui::SliderFloat("正面反射率 F0", &appearanceParameters_.fresnelBaseReflectance, 0.0f, 0.10f, "%.4f");
	if (fresnelChanged) {
		waterPlane->SetFresnelParameters(
			appearanceParameters_.fresnelReflectanceScale,
			appearanceParameters_.fresnelBaseReflectance);
	}

	if (ImGui::SliderFloat("DXR屈折ずれ量 (px)", &rtRefractionOffsetPixels_, 0.0f, 12.0f, "%.2f")) {
		WaterEditorRayTracingSettings settings = editorFacade.GetRayTracingSettings();
		settings.maxRefractionOffsetPixels = rtRefractionOffsetPixels_;
		editorFacade.ApplyRayTracingSettings(settings);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("透過 / 水色");
	bool depthFadeChanged = false;
	depthFadeChanged |= ImGui::Checkbox("Depth Fade を有効にする", &surfaceParameters_.depthFadeEnabled);
	depthFadeChanged |= ImGui::SliderFloat("吸収係数", &surfaceParameters_.absorptionCoeff, 0.0f, 8.0f, "%.3f");
	if (depthFadeChanged) {
		waterPlane->SetDepthFade(surfaceParameters_.absorptionCoeff, surfaceParameters_.depthFadeEnabled);
	}

	bool waterColorChanged = false;
	waterColorChanged |= ImGui::ColorEdit3("浅瀬の色", surfaceParameters_.shallowColor);
	waterColorChanged |= ImGui::ColorEdit3("深場の色", surfaceParameters_.deepColor);
	if (waterColorChanged) {
		waterPlane->SetWaterColors(
			{ surfaceParameters_.shallowColor[0], surfaceParameters_.shallowColor[1], surfaceParameters_.shallowColor[2] },
			{ surfaceParameters_.deepColor[0], surfaceParameters_.deepColor[1], surfaceParameters_.deepColor[2] });
	}
}

void WaterSurfaceParameterPanel::DrawWaveToolSection(WaterSurfaceRuntimeController& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	// UV アニメーションとレイヤー波生成の補助 UI を描画する
	const WaterPresetType currentPreset = static_cast<WaterPresetType>(waveToolState_.preset);
	const WaterPresetData& presetData = GetWaterPresetData(currentPreset);

	ImGui::SeparatorText("UVアニメーション");
	if (ImGui::DragFloat2("スクロール速度 (U, V)", surfaceParameters_.scrollSpeed, 0.001f, -1.0f, 1.0f, "%.4f")) {
		waterPlane->SetScrollSpeed({ surfaceParameters_.scrollSpeed[0], surfaceParameters_.scrollSpeed[1] });
	}
	if (ImGui::DragFloat2("UVタイリング (U, V)", surfaceParameters_.uvTiling, 0.1f, 0.1f, 32.0f, "%.2f")) {
		waterPlane->SetUVTiling({ surfaceParameters_.uvTiling[0], surfaceParameters_.uvTiling[1] });
	}

	ImGui::Spacing();
	ImGui::SeparatorText("波生成ツール");
	if (ImGui::Checkbox("推奨波数に固定する", &waveToolState_.lockRecommendedWaveCount)) {
		if (waveToolState_.lockRecommendedWaveCount) {
			RestoreRecommendedWaveCount(runtimeController, currentPreset);
			RegenerateLayeredWaves(runtimeController, currentPreset, ClampWaveCountToRange(waveToolState_.activeWaveCount));
		}
	}
	ImGui::Checkbox("プリセット切り替え時に推奨波数へ戻す", &waveToolState_.autoRestoreRecommendedWaveCount);
	ImGui::Checkbox("波数を増やしたときに自動再生成する", &waveToolState_.autoGenerateOnWaveCountIncrease);

	const int previousActiveWaveCount = waveToolState_.activeWaveCount;
	if (ImGui::SliderInt("有効な波数", &waveToolState_.activeWaveCount, 1, static_cast<int>(kMaxWaterWaveCount))) {
		if (waveToolState_.lockRecommendedWaveCount) {
			RestoreRecommendedWaveCount(runtimeController, currentPreset);
		} else {
			waterPlane->SetActiveWaveCount(static_cast<uint32_t>(waveToolState_.activeWaveCount));
			if (waveToolState_.autoGenerateOnWaveCountIncrease && waveToolState_.activeWaveCount > previousActiveWaveCount) {
				RegenerateLayeredWaves(runtimeController, currentPreset, ClampWaveCountToRange(waveToolState_.activeWaveCount));
			}
		}
	}

	if (ImGui::Button("レイヤー波を再生成", ImVec2(-1.0f, 0.0f))) {
		RegenerateLayeredWaves(runtimeController, currentPreset, ClampWaveCountToRange(waveToolState_.activeWaveCount));
	}

	ImGui::Text("推奨波数: %u", presetData.recommendedWaveCount);
	static const char* kLayerNames[kWaterWaveLayerCount] = { "大波", "中波", "小波" };
	for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount; ++layerIndex) {
		const WaterWaveLayerConfig& layer = presetData.layers[layerIndex];
		ImGui::BulletText(
			"%s: 本数=%u 振幅=%.3f-%.3f 波長=%.1f-%.1f",
			kLayerNames[layerIndex],
			layer.count,
			layer.amplitudeMin,
			layer.amplitudeMax,
			layer.wavelengthMin,
			layer.wavelengthMax);
	}

	DrawIndividualWaveEditor(runtimeController);
}

void WaterSurfaceParameterPanel::DrawIndividualWaveEditor(WaterSurfaceRuntimeController& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane || !ImGui::TreeNode("個別波編集")) {
		return;
	}

	// 生成済みの各波を個別に微調整できるよう展開表示する
	WaveParams* waves = waterPlane->GetWaves();
	for (uint32_t waveIndex = 0; waveIndex < kMaxWaterWaveCount; ++waveIndex) {
		ImGui::PushID(static_cast<int>(waveIndex));
		char label[32];
		std::snprintf(label, sizeof(label), "波 %u", waveIndex);
		if (ImGui::TreeNode(label)) {
			float direction[2] = { waves[waveIndex].direction.x, waves[waveIndex].direction.y };
			if (ImGui::DragFloat2("進行方向", direction, 0.01f, -1.0f, 1.0f)) {
				const float length = std::sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
				if (length > 1.0e-4f) {
					direction[0] /= length;
					direction[1] /= length;
				}
				waves[waveIndex].direction = { direction[0], direction[1] };
			}
			ImGui::DragFloat("振幅", &waves[waveIndex].amplitude, 0.01f, 0.0f, 5.0f);
			ImGui::DragFloat("波長", &waves[waveIndex].wavelength, 0.1f, 0.1f, 50.0f);
			ImGui::DragFloat("速度", &waves[waveIndex].speed, 0.05f, 0.0f, 10.0f);
			ImGui::DragFloat("急峻さ", &waves[waveIndex].steepness, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat("位相オフセット", &waves[waveIndex].phaseOffset, 0.05f, -12.56f, 12.56f);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	ImGui::TreePop();
}

#endif
