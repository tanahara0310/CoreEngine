#include "pch.h"
#include "WaterSurfaceParameterPanel.h"

#ifdef USE_IMGUI

#include "Graphics/Water/Render/WaterRenderFeature.h"
#include "Graphics/Water/Surface/WaterPlaneObject.h"
#include "Graphics/Water/WaterCVars.h"

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
	float amplitudeScale;
	float windDirection[2];
	float windSpeed;
	float choppiness;
	int activeComponentCount;
	float gravity;
};

Vector2 NormalizeDirection(const Vector2& direction);

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
// パッチ長はカスケード定数（FFTOceanCascadeValues.hlsli）で固定のためプリセットから撤去済み。
const FFTOceanPresetData kFFTOceanPresets[] = {
	{ "カスタム", 1.0f, { 0.92f, 0.38f }, 12.0f, 1.35f, 32, 9.81f },
	{ "鏡面に近い海", 1.0f, { 1.0f, 0.0f }, 2.0f, 0.10f, 8, 9.81f },
	{ "静かな海", 0.5f, { 0.98f, 0.18f }, 7.5f, 0.35f, 16, 9.81f },
	{ "うねりのある外洋", 1.0f, { 0.92f, 0.38f }, 12.0f, 1.35f, 32, 9.81f },
	{ "荒れた海", 1.0f, { 0.84f, 0.54f }, 18.0f, 1.80f, 48, 9.81f },
	{ "嵐の海", 0.9f, { 0.72f, 0.69f }, 26.0f, 2.40f, 64, 9.81f },
};

bool NearlyEqual(float lhs, float rhs, float epsilon = 1.0e-3f) {
	return std::fabs(lhs - rhs) <= epsilon;
}

/// @brief 現在の WaterCVars（FFT 群）に一致する海況プリセットを探す
/// @details 風向は正規化して比較する（CVar には UI 入力の未正規化値が入りうるため）
int FindMatchingFFTOceanPreset() {
	const Vector2 currentDir = NormalizeDirection(WaterCVars::FFTWindDirection.Get());
	for (int presetIndex = 1; presetIndex < static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresets)); ++presetIndex) {
		const FFTOceanPresetData& preset = kFFTOceanPresets[presetIndex];
		const Vector2 presetDir = NormalizeDirection({ preset.windDirection[0], preset.windDirection[1] });
		if (!NearlyEqual(WaterCVars::FFTAmplitudeScale.Get(), preset.amplitudeScale) ||
			!NearlyEqual(currentDir.x, presetDir.x) ||
			!NearlyEqual(currentDir.y, presetDir.y) ||
			!NearlyEqual(WaterCVars::FFTWindSpeed.Get(), preset.windSpeed) ||
			!NearlyEqual(WaterCVars::FFTChoppiness.Get(), preset.choppiness) ||
			WaterCVars::FFTActiveComponentCount.Get() != preset.activeComponentCount ||
			!NearlyEqual(WaterCVars::FFTGravity.Get(), preset.gravity)) {
			continue;
		}

		return presetIndex;
	}

	return kFFTOceanPresetCustom;
}
}

void WaterSurfaceParameterPanel::Initialize(WaterRenderFeature& runtimeController, [[maybe_unused]] WaterEditorFacade& editorFacade) {
	// パラメータ値には触れない: WaterCVars は CVarSettingsSection の登録時に
	// CVars.json から復元済みで、それが唯一の情報源。ここでプリセットを適用すると
	// 復元値を潰してしまう（旧実装の「既定プリセット→Deserialize 上書き」順序問題は
	// CVar の静的寿命により解消された）。
	fftPresetIndex_ = FindMatchingFFTOceanPreset();

	// Gerstner 波（非永続のワークフロー状態）だけは生成が必要
	const WaterPresetType initialPreset = static_cast<WaterPresetType>(waveToolState_.preset);
	RestoreRecommendedWaveCount(runtimeController, initialPreset);
	RegenerateLayeredWaves(runtimeController, initialPreset, ClampWaveCountToRange(waveToolState_.activeWaveCount));
}

void WaterSurfaceParameterPanel::Draw(WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade) {
	// 共通設定と方式別設定をタブで分離して、開発用 UI の見通しを良くする
	if (ImGui::CollapsingHeader("水面パラメータ", ImGuiTreeNodeFlags_DefaultOpen)) {
		const WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
		const bool usingFFTOcean = waterPlane && waterPlane->IsUsingFFTOcean();

		ImGui::Text("現在の編集中モード: %s", usingFFTOcean ? "FFTOcean" : "Gerstner Wave");
		ImGui::TextDisabled("全パラメータは CVar（r.Water.*）として保存されます。Engine Settings の CVar ツリーからも編集できます。");

		if (ImGui::BeginTabBar("WaterParameterTabs")) {
			if (ImGui::BeginTabItem("共通設定")) {
				DrawWaterTypeSection(runtimeController);
				DrawCommonParameterSection(runtimeController);
				DrawCausticsSection(editorFacade);
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

void WaterSurfaceParameterPanel::DrawWaterTypeSection(WaterRenderFeature& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	ImGui::SeparatorText("描画方式");
	ImGui::Text("現在の方式: %s", waterPlane->IsUsingFFTOcean() ? "FFTOcean" : "Gerstner Wave");
	ImGui::TextDisabled("共通設定は下部、方式固有の調整は各専用セクションに分けています。");

	const bool useFFTOcean = WaterCVars::FFTEnabled.Get();
	if (ImGui::RadioButton("FFTOcean を使用", useFFTOcean)) {
		WaterCVars::FFTEnabled.Set(true);
	}
	if (ImGui::RadioButton("Gerstner Wave を使用", !useFFTOcean)) {
		WaterCVars::FFTEnabled.Set(false);
	}
}

void WaterSurfaceParameterPanel::DrawCommonParameterSection(WaterRenderFeature& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	// CVar のストレージを ImGui へ直接渡し、変更時は NotifyChanged で通番を進める。
	// 実際の水面への適用は WaterRenderFeature::ApplySettingsFromCVars（毎フレーム）が行う。
	auto notify = [](CoreEngine::ICVar& cvar, bool changed) {
		if (changed) {
			cvar.NotifyChanged();
		}
	};

	ImGui::Spacing();
	ImGui::SeparatorText("共通の見た目");
	notify(WaterCVars::BaseColor, ImGui::ColorEdit4("ベースカラー", &WaterCVars::BaseColor.AsColor()->x));
	notify(WaterCVars::Roughness, ImGui::SliderFloat("ラフネス", WaterCVars::Roughness.AsFloat(), 0.0f, 1.0f));
	notify(WaterCVars::Metallic, ImGui::SliderFloat("メタリック", WaterCVars::Metallic.AsFloat(), 0.0f, 1.0f));
	notify(WaterCVars::IBLEnabled, ImGui::Checkbox("IBLを有効にする", WaterCVars::IBLEnabled.AsBool()));

	ImGui::Spacing();
	ImGui::SeparatorText("反射 / 屈折");
	notify(WaterCVars::FresnelScale, ImGui::SliderFloat("フレネル反射スケール", WaterCVars::FresnelScale.AsFloat(), 0.0f, 2.0f, "%.3f"));
	notify(WaterCVars::FresnelF0, ImGui::SliderFloat("正面反射率 F0", WaterCVars::FresnelF0.AsFloat(), 0.0f, 0.10f, "%.4f"));
	notify(WaterCVars::RefractionMaxOffsetPixels,
		ImGui::SliderFloat("DXR屈折 最大ずれ量 (px, 0=無制限)", WaterCVars::RefractionMaxOffsetPixels.AsFloat(), 0.0f, 64.0f, "%.2f"));

	ImGui::Spacing();
	ImGui::SeparatorText("泡 / Whitecap（FFTOcean 専用）");
	notify(WaterCVars::FoamEnabled, ImGui::Checkbox("泡を有効にする", WaterCVars::FoamEnabled.AsBool()));
	notify(WaterCVars::FoamBias, ImGui::SliderFloat("発生しきい値 detJ", WaterCVars::FoamBias.AsFloat(), 0.0f, 1.5f, "%.3f"));
	notify(WaterCVars::FoamGain, ImGui::SliderFloat("立ち上がり勾配", WaterCVars::FoamGain.AsFloat(), 0.5f, 16.0f, "%.2f"));
	notify(WaterCVars::FoamOpacity, ImGui::SliderFloat("泡の不透明度", WaterCVars::FoamOpacity.AsFloat(), 0.0f, 1.0f, "%.3f"));
	notify(WaterCVars::FoamCascadeWeights,
		ImGui::SliderFloat3("カスケード重み (大/中/小)", &WaterCVars::FoamCascadeWeights.AsVector3()->x, 0.0f, 1.0f, "%.3f"));
	notify(WaterCVars::FoamDecaySeconds, ImGui::SliderFloat("泡の寿命 [秒]", WaterCVars::FoamDecaySeconds.AsFloat(), 0.2f, 10.0f, "%.2f"));
	ImGui::TextDisabled(
		"detJ（波頭の圧縮率）がしきい値を下回ると泡。可視化は水面デバッグの\n"
		"『FFT Jacobian』『FFT 泡マスク』を使用。小パッチの重みを上げすぎると海面全体が白くなります");

	ImGui::Spacing();
	ImGui::SeparatorText("透過 / 水質（光学特性）");
	notify(WaterCVars::DepthFadeEnabled, ImGui::Checkbox("Depth Fade を有効にする", WaterCVars::DepthFadeEnabled.AsBool()));

	// Jerlov 水型プリセット: ベース係数を一括設定し、濁度をリセットする
	// （復元値が範囲外でも安全に表示できるよう、使用直前にクランプする）
	const int jerlovPreset = std::clamp(WaterCVars::JerlovPreset.Get(), 0, kJerlovWaterTypeCount - 1);
	if (ImGui::BeginCombo("水質プリセット (Jerlov)", kJerlovWaterTypes[jerlovPreset].name)) {
		for (int i = 0; i < kJerlovWaterTypeCount; ++i) {
			const bool selected = (jerlovPreset == i);
			if (ImGui::Selectable(kJerlovWaterTypes[i].name, selected)) {
				WaterCVars::JerlovPreset.Set(i);
				WaterCVars::AbsorptionBase.Set({
					kJerlovWaterTypes[i].absorptionCoeff[0],
					kJerlovWaterTypes[i].absorptionCoeff[1],
					kJerlovWaterTypes[i].absorptionCoeff[2] });
				WaterCVars::ScatteringBase.Set({
					kJerlovWaterTypes[i].scatteringCoeff[0],
					kJerlovWaterTypes[i].scatteringCoeff[1],
					kJerlovWaterTypes[i].scatteringCoeff[2] });
				WaterCVars::Turbidity.Set(0.0f);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	// 水の色は直接指定せず、波長依存の吸収・散乱係数（＝水質）から光源と合わせて導出する。
	// 永続化は必ずベース値（実効係数から逆算不能なため）。実効値の合成は WaterCVars 側。
	notify(WaterCVars::AbsorptionBase,
		ImGui::DragFloat3("吸収係数 σa (R, G, B) [1/m]", &WaterCVars::AbsorptionBase.AsVector3()->x, 0.005f, 0.0f, 4.0f, "%.4f"));
	notify(WaterCVars::ScatteringBase,
		ImGui::DragFloat3("散乱係数 σs (R, G, B) [1/m]", &WaterCVars::ScatteringBase.AsVector3()->x, 0.001f, 0.0f, 2.0f, "%.4f"));
	notify(WaterCVars::Turbidity, ImGui::SliderFloat("濁度", WaterCVars::Turbidity.AsFloat(), 0.0f, 1.0f, "%.3f"));
	ImGui::TextDisabled("自然な水は 吸収: 赤 > 緑 > 青。濁度は青の吸収と粒子散乱を加算（緑濁り方向）");

	ImGui::Spacing();
	ImGui::SeparatorText("共通 UV アニメーション");
	notify(WaterCVars::ScrollSpeed,
		ImGui::DragFloat2("スクロール速度 (U, V)", &WaterCVars::ScrollSpeed.AsVector2()->x, 0.001f, -1.0f, 1.0f, "%.4f"));
	notify(WaterCVars::UVTiling,
		ImGui::DragFloat2("UVタイリング (U, V)", &WaterCVars::UVTiling.AsVector2()->x, 0.1f, 0.1f, 32.0f, "%.2f"));
}

void WaterSurfaceParameterPanel::DrawCausticsSection(WaterEditorFacade& editorFacade) {
	ImGui::Spacing();
	ImGui::SeparatorText("コースティクス");

	const WaterEditorCausticsSettings status = editorFacade.GetCausticsSettings();
	if (!status.techniqueAvailable) {
		ImGui::TextDisabled("WaterCausticsTechnique が登録されていません。");
		return;
	}

	auto notify = [](CoreEngine::ICVar& cvar, bool changed) {
		if (changed) {
			cvar.NotifyChanged();
		}
	};

	// 生成方式（合成されるのは選んだ側だけ。もう一方のパスは実行自体をスキップする）。
	// スクリーンスペース版は Gerstner 専用（FFT シーンでは何も出ない）
	static const char* kCausticsBackends[] = {
		"レイトレーシング (DXR)",
		"スクリーンスペース (Gerstner 専用)",
	};
	int backend = std::clamp(WaterCVars::CausticsBackend.Get(), 0, 1);
	if (ImGui::Combo("生成方式", &backend, kCausticsBackends, IM_ARRAYSIZE(kCausticsBackends))) {
		WaterCVars::CausticsBackend.Set(backend);
	}

	notify(WaterCVars::CausticsIntensity, ImGui::SliderFloat("強度", WaterCVars::CausticsIntensity.AsFloat(), 0.0f, 8.0f, "%.3f"));
	notify(WaterCVars::CausticsDepthAttenuation, ImGui::SliderFloat("深度減衰", WaterCVars::CausticsDepthAttenuation.AsFloat(), 0.0f, 4.0f, "%.3f"));
	notify(WaterCVars::CausticsCurvatureScale, ImGui::SliderFloat("曲率スケール", WaterCVars::CausticsCurvatureScale.AsFloat(), 0.0f, 30.0f, "%.3f"));
	notify(WaterCVars::CausticsSurfaceSampleRadius, ImGui::SliderFloat("水面サンプル半径", WaterCVars::CausticsSurfaceSampleRadius.AsFloat(), 0.05f, 2.0f, "%.3f"));
	notify(WaterCVars::CausticsRefractiveIndex, ImGui::SliderFloat("屈折率", WaterCVars::CausticsRefractiveIndex.AsFloat(), 1.0f, 1.6f, "%.4f"));
	notify(WaterCVars::CausticsReceiverNormalStrength, ImGui::SliderFloat("受光面法線強度", WaterCVars::CausticsReceiverNormalStrength.AsFloat(), 0.0f, 2.0f, "%.3f"));
	notify(WaterCVars::CausticsAlignmentPower, ImGui::SliderFloat("フォーカス強度", WaterCVars::CausticsAlignmentPower.AsFloat(), 1.0f, 64.0f, "%.3f"));
	ImGui::TextDisabled("デバッグ表示・可視化モードは下の『デバッグ / 診断』にあります。");
}

void WaterSurfaceParameterPanel::DrawFFTOceanSection(
	WaterRenderFeature& runtimeController,
	WaterEditorFacade& editorFacade) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	ImGui::Text("状態: %s", waterPlane->IsUsingFFTOcean() ? "使用中" : "待機中");
	ImGui::TextDisabled("広域の海面調整用です。現在使っていなくても事前に設定を整えておけます。");
	if (!waterPlane->IsUsingFFTOcean() && ImGui::Button("この方式へ切り替える", ImVec2(-1.0f, 0.0f))) {
		WaterCVars::FFTEnabled.Set(true);
	}

	const WaterEditorFFTSettings status = editorFacade.GetFFTSettings();
	if (!status.managerAvailable) {
		ImGui::TextDisabled("FFTOceanManager を取得できません。");
		return;
	}

	// 外部（CVar ツリー等）からの変更でプリセット一致が変わりうるため毎フレーム再判定する。
	// カスタム選択中はそのまま維持する（値が偶然一致した場合のみプリセット表示へ戻る）
	fftPresetIndex_ = (fftPresetIndex_ == kFFTOceanPresetCustom)
		? FindMatchingFFTOceanPreset()
		: fftPresetIndex_;

	ImGui::SeparatorText("海況プリセット");
	const int previousPreset = fftPresetIndex_;
	if (ImGui::Button("前のプリセット##fftPreset")) {
		fftPresetIndex_ = (fftPresetIndex_ <= 1)
			? static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresetNames)) - 1
			: (fftPresetIndex_ - 1);
		ApplyFFTOceanPreset(fftPresetIndex_);
	}
	ImGui::SameLine();
	if (ImGui::Button("次のプリセット##fftPreset")) {
		fftPresetIndex_ = (fftPresetIndex_ >= static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresetNames)) - 1)
			? 1
			: (fftPresetIndex_ + 1);
		ApplyFFTOceanPreset(fftPresetIndex_);
	}
	ImGui::Combo("FFT Ocean プリセット", &fftPresetIndex_, kFFTOceanPresetNames, IM_ARRAYSIZE(kFFTOceanPresetNames));
	if (fftPresetIndex_ != previousPreset && fftPresetIndex_ != kFFTOceanPresetCustom) {
		ApplyFFTOceanPreset(fftPresetIndex_);
	}
	ImGui::Text("現在の海況: %s", kFFTOceanPresetNames[std::clamp(fftPresetIndex_, 0, static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresetNames)) - 1)]);
	ImGui::TextDisabled("静穏な海から荒天まで、海面の傾向をまとめて切り替えます。\n手動調整すると自動でカスタムへ戻ります。");

	if (fftPresetIndex_ != kFFTOceanPresetCustom && ImGui::Button("現在のプリセットを再適用", ImVec2(-1.0f, 0.0f))) {
		ApplyFFTOceanPreset(fftPresetIndex_);
	}

	ImGui::Spacing();
	ImGui::SeparatorText("FFT Ocean 詳細");
	ImGui::Text("解像度: %d (再生成が必要なため表示のみ)", status.resolution);

	bool fftChanged = false;
	auto trackFFTEdit = [&](CoreEngine::ICVar& cvar, bool changed) {
		if (changed) {
			cvar.NotifyChanged();
			fftChanged = true;
		}
	};

	trackFFTEdit(WaterCVars::FFTAmplitudeScale, ImGui::SliderFloat("振幅スケール", WaterCVars::FFTAmplitudeScale.AsFloat(), 0.0f, 4.0f, "%.3f"));
	trackFFTEdit(WaterCVars::FFTWindDirection, ImGui::DragFloat2("風向", &WaterCVars::FFTWindDirection.AsVector2()->x, 0.01f, -1.0f, 1.0f, "%.3f"));
	trackFFTEdit(WaterCVars::FFTWindSpeed, ImGui::SliderFloat("風速", WaterCVars::FFTWindSpeed.AsFloat(), 0.0f, 64.0f, "%.2f"));
	trackFFTEdit(WaterCVars::FFTChoppiness, ImGui::SliderFloat("Choppiness", WaterCVars::FFTChoppiness.AsFloat(), 0.0f, 4.0f, "%.3f"));
	trackFFTEdit(WaterCVars::FFTActiveComponentCount, ImGui::SliderInt("スペクトル成分数", WaterCVars::FFTActiveComponentCount.AsInt(), 1, 64));
	trackFFTEdit(WaterCVars::FFTGravity, ImGui::SliderFloat("重力", WaterCVars::FFTGravity.AsFloat(), 0.1f, 20.0f, "%.3f"));

	if (fftChanged) {
		// 手動調整はカスタム扱い（値がプリセットへ偶然一致すれば表示は自動で戻る）
		fftPresetIndex_ = FindMatchingFFTOceanPreset();
	}

	if (ImGui::Button("FFT Ocean 設定を既定値へ戻す", ImVec2(-1.0f, 0.0f))) {
		WaterCVars::FFTAmplitudeScale.ResetToDefault();
		WaterCVars::FFTWindDirection.ResetToDefault();
		WaterCVars::FFTWindSpeed.ResetToDefault();
		WaterCVars::FFTChoppiness.ResetToDefault();
		WaterCVars::FFTActiveComponentCount.ResetToDefault();
		WaterCVars::FFTGravity.ResetToDefault();
		fftPresetIndex_ = FindMatchingFFTOceanPreset();
	}
}

void WaterSurfaceParameterPanel::ApplyFFTOceanPreset(int presetIndex) {
	if (presetIndex <= kFFTOceanPresetCustom || presetIndex >= static_cast<int>(IM_ARRAYSIZE(kFFTOceanPresets))) {
		return;
	}

	// プリセットは WaterCVars への一括 Set で表現する。
	// 適用（FFTOceanManager::SetSettings＝スペクトル再構築）は
	// WaterRenderFeature が revision 変化を検知して行う
	const FFTOceanPresetData& preset = kFFTOceanPresets[presetIndex];
	fftPresetIndex_ = presetIndex;
	WaterCVars::FFTAmplitudeScale.Set((std::max)(preset.amplitudeScale, 0.0f));
	WaterCVars::FFTWindDirection.Set(NormalizeDirection({ preset.windDirection[0], preset.windDirection[1] }));
	WaterCVars::FFTWindSpeed.Set((std::max)(preset.windSpeed, 0.0f));
	WaterCVars::FFTChoppiness.Set((std::max)(preset.choppiness, 0.0f));
	WaterCVars::FFTActiveComponentCount.Set((std::clamp)(preset.activeComponentCount, 1, 64));
	WaterCVars::FFTGravity.Set((std::max)(preset.gravity, 0.1f));
}

void WaterSurfaceParameterPanel::ApplyWaterPreset(WaterRenderFeature& runtimeController, WaterPresetType preset) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	// プリセット値を WaterCVars（単一情報源）へ適用する。
	// 水面への実反映は WaterRenderFeature::ApplySettingsFromCVars（毎フレーム）が行う
	const WaterPresetData& presetData = GetWaterPresetData(preset);
	waveToolState_.preset = static_cast<int>(preset);

	WaterCVars::BaseColor.Set(presetData.baseColor);
	WaterCVars::Roughness.Set(presetData.roughness);
	WaterCVars::Metallic.Set(presetData.metallic);
	WaterCVars::FresnelScale.Set(presetData.fresnelReflectanceScale);
	WaterCVars::FresnelF0.Set(presetData.fresnelBaseReflectance);
	WaterCVars::AbsorptionBase.Set(presetData.absorptionCoeff);
	WaterCVars::ScatteringBase.Set(presetData.scatteringCoeff);
	WaterCVars::Turbidity.Set(0.0f);
	WaterCVars::ScrollSpeed.Set(presetData.scrollSpeed);
	WaterCVars::UVTiling.Set(presetData.uvTiling);

	// 必要に応じて推奨波数へ戻し、プリセットに応じた波を再生成する
	if (waveToolState_.lockRecommendedWaveCount || waveToolState_.autoRestoreRecommendedWaveCount) {
		RestoreRecommendedWaveCount(runtimeController, preset);
	}

	RegenerateLayeredWaves(runtimeController, preset, ClampWaveCountToRange(waveToolState_.activeWaveCount));
}

void WaterSurfaceParameterPanel::RestoreRecommendedWaveCount(WaterRenderFeature& runtimeController, WaterPresetType preset) {
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
	WaterRenderFeature& runtimeController,
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

void WaterSurfaceParameterPanel::DrawGerstnerWaveSection(WaterRenderFeature& runtimeController) {
	WaterPlaneObject* waterPlane = runtimeController.GetWaterPlane();
	if (!waterPlane) {
		return;
	}

	ImGui::Text("状態: %s", waterPlane->IsUsingFFTOcean() ? "待機中" : "使用中");
	ImGui::TextDisabled("局所的な波の作り込みや、個別波の直接編集はこちらで行います。");
	if (waterPlane->IsUsingFFTOcean() && ImGui::Button("この方式へ切り替える", ImVec2(-1.0f, 0.0f))) {
		WaterCVars::FFTEnabled.Set(false);
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

void WaterSurfaceParameterPanel::DrawIndividualWaveEditor(WaterRenderFeature& runtimeController) {
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
