#include "pch.h"
#include "WaterSurfaceParameterPanel.h"

#ifdef USE_IMGUI

#include "WaterSurfaceRuntimeController.h"

#include "Math/MathCore.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Utility/Random/RandomGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <numbers>

using namespace CoreEngine;

namespace {
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;

/// @brief Jerlov 水型に基づく水質プリセット（σa/σs はアート調整済みの近似値 [1/m]）
/// @details Jerlov 分類は外洋 I〜III、沿岸 1C〜9C の順に濁っていく。
///          濁るほど CDOM（溶存有機物）が短波長（青）を強く吸収するため、
///          澄んだ水の「青」から沿岸の「緑」へ色相が移る。
struct JerlovWaterType {
	const char* name;
	float absorptionCoeff[3];
	float scatteringCoeff[3];
};

constexpr JerlovWaterType kJerlovWaterTypes[] = {
	{ "I（熱帯外洋・沖縄）",   { 0.35f, 0.07f, 0.02f }, { 0.003f, 0.008f, 0.016f } },
	{ "IB（澄んだ外洋）",      { 0.38f, 0.09f, 0.04f }, { 0.006f, 0.012f, 0.020f } },
	{ "II（外洋）",            { 0.45f, 0.13f, 0.09f }, { 0.010f, 0.018f, 0.026f } },
	{ "III（沿岸寄り外洋）",   { 0.52f, 0.18f, 0.15f }, { 0.018f, 0.028f, 0.035f } },
	{ "1C（澄んだ沿岸）",      { 0.55f, 0.17f, 0.20f }, { 0.030f, 0.045f, 0.045f } },
	{ "5C（沿岸）",            { 0.65f, 0.30f, 0.45f }, { 0.080f, 0.100f, 0.085f } },
	{ "9C（濁った沿岸）",      { 0.85f, 0.50f, 0.90f }, { 0.180f, 0.200f, 0.150f } },
};
constexpr int kJerlovWaterTypeCount = static_cast<int>(std::size(kJerlovWaterTypes));

// 濁度 1.0 のときにベース係数へ加算する量 [1/m]。
// 吸収は CDOM・植物プランクトンによる短波長（青）優位の吸収、
// 散乱は懸濁粒子によるほぼ無選択（わずかに短波長寄り）の散乱を表す。
constexpr float kTurbidityAbsorptionGain[3] = { 0.05f, 0.08f, 0.35f };
constexpr float kTurbidityScatteringGain[3] = { 0.22f, 0.26f, 0.20f };

enum FFTOceanPresetIndex : int {
	kFFTOceanPresetCustom = 0,
	kFFTOceanPresetMirrorLike = 1,
	kFFTOceanPresetCalm = 2,
	kFFTOceanPresetOpenSea = 3,
	kFFTOceanPresetRough = 4,
	kFFTOceanPresetStorm = 5,
};

struct FFTOceanPresetData {
	const char* name;
	float patchLength;
	float amplitudeScale;
	float windDirection[2];
	float windSpeed;
	float choppiness;
	int activeComponentCount;
	float gravity;
};

Vector2 NormalizeDirection(const Vector2& direction);

WaterEditorFFTSettings BuildSanitizedFFTOceanSettings(const FFTOceanPresetData& preset) {
	WaterEditorFFTSettings settings{};
	settings.patchLength = (std::max)(preset.patchLength, 1.0f);
	settings.amplitudeScale = (std::max)(preset.amplitudeScale, 0.0f);
	settings.windSpeed = (std::max)(preset.windSpeed, 0.0f);
	settings.choppiness = (std::max)(preset.choppiness, 0.0f);
	settings.activeComponentCount = (std::clamp)(preset.activeComponentCount, 1, 64);
	settings.gravity = (std::max)(preset.gravity, 0.1f);

	const Vector2 normalizedWindDirection = NormalizeDirection({ preset.windDirection[0], preset.windDirection[1] });
	settings.windDirection[0] = normalizedWindDirection.x;
	settings.windDirection[1] = normalizedWindDirection.y;
	return settings;
}

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

const char* const kFFTOceanPresetNames[] = {
	"カスタム",
	"鏡面に近い海",
	"静かな海",
	"うねりのある外洋",
	"荒れた海",
	"嵐の海",
};

// 波高は Pierson-Moskowitz 較正（Hs ≈ 0.21 v²/g）で風速から決まるため、
// amplitudeScale は「較正済み波高に対する相対倍率」（基本 1.0 前後）とする。
// 旧テーブルの大きな amplitudeScale（1.85〜2.8）は較正前の壊れたスケールへの
// 補正値だったので、そのまま使うと非現実的な波高になる。
// 目安: 静かな海 Hs≈0.6m / 外洋 Hs≈3m / 荒れた海 Hs≈7m / 嵐 Hs≈13m。
const FFTOceanPresetData kFFTOceanPresets[] = {
	{ "カスタム", 96.0f, 1.0f, { 0.92f, 0.38f }, 12.0f, 1.35f, 32, 9.81f },
	{ "鏡面に近い海", 220.0f, 1.0f, { 1.0f, 0.0f }, 2.0f, 0.10f, 8, 9.81f },
	{ "静かな海", 180.0f, 0.5f, { 0.98f, 0.18f }, 7.5f, 0.35f, 16, 9.81f },
	{ "うねりのある外洋", 96.0f, 1.0f, { 0.92f, 0.38f }, 12.0f, 1.35f, 32, 9.81f },
	{ "荒れた海", 72.0f, 1.0f, { 0.84f, 0.54f }, 18.0f, 1.80f, 48, 9.81f },
	{ "嵐の海", 56.0f, 0.9f, { 0.72f, 0.69f }, 26.0f, 2.40f, 64, 9.81f },
};

bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-3f) {
	return std::fabs(lhs - rhs) <= epsilon;
}

int FindMatchingFFTOceanPreset(const WaterEditorFFTSettings& settings) {
	for (int presetIndex = 1; presetIndex < static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresets)); ++presetIndex) {
		const WaterEditorFFTSettings presetSettings = BuildSanitizedFFTOceanSettings(kFFTOceanPresets[presetIndex]);
		if (!NearlyEqual(settings.patchLength, presetSettings.patchLength) ||
			!NearlyEqual(settings.amplitudeScale, presetSettings.amplitudeScale) ||
			!NearlyEqual(settings.windDirection[0], presetSettings.windDirection[0]) ||
			!NearlyEqual(settings.windDirection[1], presetSettings.windDirection[1]) ||
			!NearlyEqual(settings.windSpeed, presetSettings.windSpeed) ||
			!NearlyEqual(settings.choppiness, presetSettings.choppiness) ||
			settings.activeComponentCount != presetSettings.activeComponentCount ||
			!NearlyEqual(settings.gravity, presetSettings.gravity)) {
			continue;
		}

		return presetIndex;
	}

	return kFFTOceanPresetCustom;
}
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
	fftOceanParameters_.preset = FindMatchingFFTOceanPreset(fftSettings);

	// 現在の DXR 屈折設定を UI の初期値へ反映する
	rtRefractionOffsetPixels_ = editorFacade.GetRayTracingSettings().maxRefractionOffsetPixels;

	// 既定プリセットを適用して UI と水面状態を同期する
	ApplyWaterPreset(runtimeController, static_cast<WaterPresetType>(waveToolState_.preset));
}

void WaterSurfaceParameterPanel::Draw(WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade) {
	// 共通設定と方式別設定をタブで分離して、開発用 UI の見通しを良くする
	if (ImGui::CollapsingHeader("水面パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
		const WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
		const bool usingFFTOcean = waterPlane && waterPlane->IsUsingFFTOcean();

		ImGui::Text("現在の編集中モード: %s", usingFFTOcean ? "FFTOcean" : "Gerstner Wave");
		ImGui::TextDisabled("共通設定と方式別設定をタブで切り替えて編集できます。");

		if (ImGui::BeginTabBar("WaterParameterTabs")) {
			if (ImGui::BeginTabItem("共通設定")) {
				DrawWaterTypeSection(runtimeController, editorFacade);
				DrawCommonParameterSection(runtimeController, editorFacade);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(usingFFTOcean ? "FFTOcean *" : "FFTOcean")) {
				DrawFFTOceanSection(runtimeController, editorFacade);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(usingFFTOcean ? "Gerstner Wave" : "Gerstner Wave *")) {
				DrawGerstnerWaveSection(runtimeController);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
}

void WaterSurfaceParameterPanel::DrawWaterTypeSection(
	WaterSurfaceRuntimeController& runtimeController,
	WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	ImGui::SeparatorText("描画方式");
	ImGui::Text("現在の方式: %s", waterPlane->IsUsingFFTOcean() ? "FFTOcean" : "Gerstner Wave");
	ImGui::TextDisabled("共通設定は下部、方式固有の調整は各専用セクションに分けています。");

	bool useFFTOcean = waterPlane->IsUsingFFTOcean();
	bool switched = false;
	if (ImGui::RadioButton("FFTOcean を使用", useFFTOcean)) {
		useFFTOcean = true;
		switched = true;
	}
	if (ImGui::RadioButton("Gerstner Wave を使用", !useFFTOcean)) {
		useFFTOcean = false;
		switched = true;
	}

	if (switched) {
		WaterEditorFFTSettings settings = editorFacade.GetFFTSettings();
		settings.enabled = useFFTOcean;
		editorFacade.ApplyFFTSettings(settings);
	}
}

void WaterSurfaceParameterPanel::ApplyEffectiveOpticalCoefficients(WaterPlaneObject* waterPlane) const {
	if (!waterPlane) {
		return;
	}

	// 実効係数 = ベース水質 + 濁度による加算（CDOM の青吸収・懸濁粒子の散乱）
	float effectiveAbsorption[3]{};
	float effectiveScattering[3]{};
	for (int c = 0; c < 3; ++c) {
		effectiveAbsorption[c] = surfaceParameters_.absorptionCoeff[c] + surfaceParameters_.turbidity * kTurbidityAbsorptionGain[c];
		effectiveScattering[c] = surfaceParameters_.scatteringCoeff[c] + surfaceParameters_.turbidity * kTurbidityScatteringGain[c];
	}

	waterPlane->SetWaterOpticalCoefficients(
		{ effectiveAbsorption[0], effectiveAbsorption[1], effectiveAbsorption[2] },
		{ effectiveScattering[0], effectiveScattering[1], effectiveScattering[2] });
}

void WaterSurfaceParameterPanel::DrawCommonParameterSection(
	WaterSurfaceRuntimeController& runtimeController,
	WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	ImGui::Spacing();
	ImGui::SeparatorText("共通の見た目");
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

	if (ImGui::SliderFloat("DXR屈折 最大ずれ量 (px, 0=無制限)", &rtRefractionOffsetPixels_, 0.0f, 64.0f, "%.2f")) {
		WaterEditorRayTracingSettings settings = editorFacade.GetRayTracingSettings();
		settings.maxRefractionOffsetPixels = rtRefractionOffsetPixels_;
		editorFacade.ApplyRayTracingSettings(settings);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("透過 / 水質（光学特性）");
	if (ImGui::Checkbox("Depth Fade を有効にする", &surfaceParameters_.depthFadeEnabled)) {
		waterPlane->SetDepthFade(surfaceParameters_.depthFadeEnabled);
	}

	// Jerlov 水型プリセット: ベース係数を一括設定し、濁度をリセットする
	// （復元値が範囲外でも安全に表示できるよう、使用直前にクランプする）
	surfaceParameters_.jerlovPreset = std::clamp(surfaceParameters_.jerlovPreset, 0, kJerlovWaterTypeCount - 1);
	if (ImGui::BeginCombo("水質プリセット (Jerlov)", kJerlovWaterTypes[surfaceParameters_.jerlovPreset].name)) {
		for (int i = 0; i < kJerlovWaterTypeCount; ++i) {
			const bool selected = (surfaceParameters_.jerlovPreset == i);
			if (ImGui::Selectable(kJerlovWaterTypes[i].name, selected)) {
				surfaceParameters_.jerlovPreset = i;
				for (int c = 0; c < 3; ++c) {
					surfaceParameters_.absorptionCoeff[c] = kJerlovWaterTypes[i].absorptionCoeff[c];
					surfaceParameters_.scatteringCoeff[c] = kJerlovWaterTypes[i].scatteringCoeff[c];
				}
				surfaceParameters_.turbidity = 0.0f;
				ApplyEffectiveOpticalCoefficients(waterPlane);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	// 水の色は直接指定せず、波長依存の吸収・散乱係数（＝水質）から光源と合わせて導出する
	bool opticalChanged = false;
	opticalChanged |= ImGui::DragFloat3("吸収係数 σa (R, G, B) [1/m]", surfaceParameters_.absorptionCoeff, 0.005f, 0.0f, 4.0f, "%.4f");
	opticalChanged |= ImGui::DragFloat3("散乱係数 σs (R, G, B) [1/m]", surfaceParameters_.scatteringCoeff, 0.001f, 0.0f, 2.0f, "%.4f");
	opticalChanged |= ImGui::SliderFloat("濁度", &surfaceParameters_.turbidity, 0.0f, 1.0f, "%.3f");
	if (opticalChanged) {
		ApplyEffectiveOpticalCoefficients(waterPlane);
	}
	ImGui::TextDisabled("自然な水は 吸収: 赤 > 緑 > 青。濁度は青の吸収と粒子散乱を加算（緑濁り方向）");

	ImGui::Spacing();
	ImGui::SeparatorText("共通 UV アニメーション");
	if (ImGui::DragFloat2("スクロール速度 (U, V)", surfaceParameters_.scrollSpeed, 0.001f, -1.0f, 1.0f, "%.4f")) {
		waterPlane->SetScrollSpeed({ surfaceParameters_.scrollSpeed[0], surfaceParameters_.scrollSpeed[1] });
	}
	if (ImGui::DragFloat2("UVタイリング (U, V)", surfaceParameters_.uvTiling, 0.1f, 0.1f, 32.0f, "%.2f")) {
		waterPlane->SetUVTiling({ surfaceParameters_.uvTiling[0], surfaceParameters_.uvTiling[1] });
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
	if (!ImGui::IsAnyItemActive()) {
		fftOceanParameters_.enabled = currentFFTSettings.enabled;
		fftOceanParameters_.patchLength = currentFFTSettings.patchLength;
		fftOceanParameters_.amplitudeScale = currentFFTSettings.amplitudeScale;
		fftOceanParameters_.windDirection[0] = currentFFTSettings.windDirection[0];
		fftOceanParameters_.windDirection[1] = currentFFTSettings.windDirection[1];
		fftOceanParameters_.windSpeed = currentFFTSettings.windSpeed;
		fftOceanParameters_.choppiness = currentFFTSettings.choppiness;
		fftOceanParameters_.activeComponentCount = currentFFTSettings.activeComponentCount;
		fftOceanParameters_.gravity = currentFFTSettings.gravity;
		fftOceanParameters_.resolution = currentFFTSettings.resolution;

		if (fftOceanParameters_.preset != kFFTOceanPresetCustom) {
			fftOceanParameters_.preset = FindMatchingFFTOceanPreset(currentFFTSettings);
		}
	}

	ImGui::Text("状態: %s", waterPlane->IsUsingFFTOcean() ? "使用中" : "待機中");
	ImGui::TextDisabled("広域の海面調整用です。現在使っていなくても事前に設定を整えておけます。");
	if (!waterPlane->IsUsingFFTOcean() && ImGui::Button("この方式へ切り替える", ImVec2(-1.0f, 0.0f))) {
		WaterEditorFFTSettings updatedSettings = editorFacade.GetFFTSettings();
		updatedSettings.enabled = true;
		editorFacade.ApplyFFTSettings(updatedSettings);
		fftOceanParameters_.enabled = true;
	}

	if (!currentFFTSettings.managerAvailable) {
		ImGui::TextDisabled("FFTOceanManager を取得できません。");
		return;
	}

	ImGui::SeparatorText("海況プリセット");
	const int previousPreset = fftOceanParameters_.preset;
	if (ImGui::Button("前のプリセット##fftPreset")) {
		const int nextPreset = (fftOceanParameters_.preset <= 1)
			? static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresetNames)) - 1
			: (fftOceanParameters_.preset - 1);
		fftOceanParameters_.preset = nextPreset;
		ApplyFFTOceanPreset(editorFacade, fftOceanParameters_.preset);
	}
	ImGui::SameLine();
	if (ImGui::Button("次のプリセット##fftPreset")) {
		const int nextPreset = (fftOceanParameters_.preset >= static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresetNames)) - 1)
			? 1
			: (fftOceanParameters_.preset + 1);
		fftOceanParameters_.preset = nextPreset;
		ApplyFFTOceanPreset(editorFacade, fftOceanParameters_.preset);
	}
	ImGui::Combo("FFT Ocean プリセット", &fftOceanParameters_.preset, kFFTOceanPresetNames, IM_ARRAYSIZE(kFFTOceanPresetNames));
	if (fftOceanParameters_.preset != previousPreset && fftOceanParameters_.preset != kFFTOceanPresetCustom) {
		ApplyFFTOceanPreset(editorFacade, fftOceanParameters_.preset);
	}
	ImGui::Text("現在の海況: %s", kFFTOceanPresetNames[fftOceanParameters_.preset]);
	ImGui::TextDisabled("静穏な海から荒天まで、海面の傾向をまとめて切り替えます。\n手動調整すると自動でカスタムへ戻ります。");

	if (fftOceanParameters_.preset != kFFTOceanPresetCustom && ImGui::Button("現在のプリセットを再適用", ImVec2(-1.0f, 0.0f))) {
		ApplyFFTOceanPreset(editorFacade, fftOceanParameters_.preset);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("FFT Ocean 詳細");
	ImGui::Text("解像度: %d (再生成が必要なため表示のみ)", fftOceanParameters_.resolution);
	const bool isCustomPreset = (fftOceanParameters_.preset == kFFTOceanPresetCustom);
	if (!isCustomPreset) {
		ImGui::TextDisabled("プリセット適用中は詳細パラメータをロックしています。\n「カスタム」を選択すると調整できます。");
	}
	ImGui::BeginDisabled(!isCustomPreset);
	bool fftChanged = false;
	auto trackFFTEdit = [&](bool changed) {
		fftChanged |= changed;
	};

	trackFFTEdit(ImGui::SliderFloat("パッチ長", &fftOceanParameters_.patchLength, 8.0f, 512.0f, "%.2f"));
	trackFFTEdit(ImGui::SliderFloat("振幅スケール", &fftOceanParameters_.amplitudeScale, 0.0f, 4.0f, "%.3f"));
	trackFFTEdit(ImGui::DragFloat2("風向", fftOceanParameters_.windDirection, 0.01f, -1.0f, 1.0f, "%.3f"));
	trackFFTEdit(ImGui::SliderFloat("風速", &fftOceanParameters_.windSpeed, 0.0f, 64.0f, "%.2f"));
	trackFFTEdit(ImGui::SliderFloat("Choppiness", &fftOceanParameters_.choppiness, 0.0f, 4.0f, "%.3f"));
	trackFFTEdit(ImGui::SliderInt("スペクトル成分数", &fftOceanParameters_.activeComponentCount, 1, 64));
	trackFFTEdit(ImGui::SliderFloat("重力", &fftOceanParameters_.gravity, 0.1f, 20.0f, "%.3f"));
	ImGui::EndDisabled();

	if (isCustomPreset && fftChanged) {
		fftOceanParameters_.preset = kFFTOceanPresetCustom;
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
		fftOceanParameters_.preset = FindMatchingFFTOceanPreset(appliedSettings);
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

void WaterSurfaceParameterPanel::ApplyFFTOceanPreset(WaterEditorFacade& editorFacade, int presetIndex) {
	if (presetIndex <= kFFTOceanPresetCustom || presetIndex >= static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresets))) {
		return;
	}

	const FFTOceanPresetData& preset = kFFTOceanPresets[presetIndex];
	fftOceanParameters_.preset = presetIndex;
	fftOceanParameters_.patchLength = preset.patchLength;
	fftOceanParameters_.amplitudeScale = preset.amplitudeScale;
	fftOceanParameters_.windDirection[0] = preset.windDirection[0];
	fftOceanParameters_.windDirection[1] = preset.windDirection[1];
	fftOceanParameters_.windSpeed = preset.windSpeed;
	fftOceanParameters_.choppiness = preset.choppiness;
	fftOceanParameters_.activeComponentCount = preset.activeComponentCount;
	fftOceanParameters_.gravity = preset.gravity;

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

	surfaceParameters_.absorptionCoeff[0] = presetData.absorptionCoeff.x;
	surfaceParameters_.absorptionCoeff[1] = presetData.absorptionCoeff.y;
	surfaceParameters_.absorptionCoeff[2] = presetData.absorptionCoeff.z;
	surfaceParameters_.scatteringCoeff[0] = presetData.scatteringCoeff.x;
	surfaceParameters_.scatteringCoeff[1] = presetData.scatteringCoeff.y;
	surfaceParameters_.scatteringCoeff[2] = presetData.scatteringCoeff.z;
	surfaceParameters_.turbidity = 0.0f;
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
	waterPlane->SetDepthFade(surfaceParameters_.depthFadeEnabled);
	ApplyEffectiveOpticalCoefficients(waterPlane);
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

void WaterSurfaceParameterPanel::DrawGerstnerWaveSection(WaterSurfaceRuntimeController& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	ImGui::Text("状態: %s", waterPlane->IsUsingFFTOcean() ? "待機中" : "使用中");
	ImGui::TextDisabled("局所的な波の作り込みや、個別波の直接編集はこちらで行います。");
	if (waterPlane->IsUsingFFTOcean() && ImGui::Button("この方式へ切り替える", ImVec2(-1.0f, 0.0f))) {
		waterPlane->SetUseFFTOcean(false);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("プリセット");
	const int previousPreset = waveToolState_.preset;
	if (ImGui::Button("前のプリセット##gerstnerPreset")) {
		waveToolState_.preset = (waveToolState_.preset <= 0)
			? (IM_ARRAYSIZE(kPresetNames) - 1)
			: (waveToolState_.preset - 1);
		ApplyWaterPreset(runtimeController, static_cast<WaterPresetType>(waveToolState_.preset));
	}
	ImGui::SameLine();
	if (ImGui::Button("次のプリセット##gerstnerPreset")) {
		waveToolState_.preset = (waveToolState_.preset >= IM_ARRAYSIZE(kPresetNames) - 1)
			? 0
			: (waveToolState_.preset + 1);
		ApplyWaterPreset(runtimeController, static_cast<WaterPresetType>(waveToolState_.preset));
	}
	ImGui::Combo("水面プリセット", &waveToolState_.preset, kPresetNames, IM_ARRAYSIZE(kPresetNames));
	if (waveToolState_.preset != previousPreset) {
		ApplyWaterPreset(runtimeController, static_cast<WaterPresetType>(waveToolState_.preset));
	}
	ImGui::Text("現在のプリセット: %s", kPresetNames[waveToolState_.preset]);

	const WaterPresetType currentPreset = static_cast<WaterPresetType>(waveToolState_.preset);
	const WaterPresetData& presetData = GetWaterPresetData(currentPreset);

	if (ImGui::Button("プリセットを再適用", ImVec2(-1.0f, 0.0f))) {
		ApplyWaterPreset(runtimeController, currentPreset);
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
