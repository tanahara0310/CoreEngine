#include "pch.h"
#include "Editor/Water/WaterSurfaceParameterPanel.h"

#ifdef CORE_EDITOR

#include "Graphics/Water/Foam/WaterWhitecapFoam.h"
#include "Graphics/Water/System/WaterSystem.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/WaterCVars.h"

#include "Editor/Command/EditorCommandStack.h"
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Math/MathCore.h"
#include "Reflection/PropertySerializer.h"
#include "Scene/Environment/WindCVars.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/CVar/CVarUndoStack.h"
#include "Utility/Path/ProjectPaths.h"
#include "Utility/Random/RandomGenerator.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <numbers>
#include <string>

using namespace CoreEngine;

namespace {
constexpr float kTwoPi = std::numbers::pi_v<float> * 2.0f;

/// プロファイルの操作のボタンの文字
constexpr const char* kWriteProfileLabel = "プロファイルへ書き込む";
constexpr const char* kReloadProfileLabel = "読み直す";
constexpr const char* kCreateProfileLabel = "プロファイルとして保存";

/// 新しく作るプロファイルを置くフォルダ
constexpr const char* kProfileFolder = "Application/Assets/Water";

/// @brief 海況のプリセット（風波は風速と吹送距離から、うねりは高さと周期から決まる）
struct SeaStatePreset {
	const char* name;
	const char* description;
	float windSpeed;      ///< [m/s]
	float fetchKm;        ///< [km]
	float amplitudeScale;
	float choppiness;
	int componentCount;
	bool swellEnabled;
	float swellHeight;    ///< [m]
	float swellPeriod;    ///< [s]
};

constexpr SeaStatePreset kSeaStatePresets[] = {
	{ "鏡のような凪", "風がほとんど無く、遠くから届く小さなうねりだけが残る海。",
	  1.5f, 5.0f, 1.0f, 1.2f, 48, true, 0.3f, 10.0f },
	{ "穏やかな入り江", "弱い風が短い距離を吹く程度。さざ波と小さなうねり。",
	  5.0f, 5.0f, 1.0f, 1.4f, 48, true, 0.4f, 10.0f },
	{ "晴れた日の外洋", "並の風が長い距離を吹いて育った、長くなだらかな波。",
	  8.0f, 100.0f, 1.0f, 1.6f, 48, true, 1.0f, 12.0f },
	{ "風の強い日", "強い風が短い距離だけ吹いた、短く尖った波。白波が出始める。",
	  13.0f, 20.0f, 1.15f, 1.8f, 64, true, 0.95f, 12.0f },
	{ "荒れた海", "強い風が長く吹いた高い波と大きなうねり。白波が多い。",
	  18.0f, 200.0f, 1.0f, 1.8f, 64, true, 2.0f, 14.0f },
	{ "嵐", "暴風で育ちきった大波。水面の多くが泡で白くなる。",
	  26.0f, 500.0f, 1.0f, 2.0f, 64, true, 3.5f, 16.0f },
};
constexpr int kSeaStatePresetCount = static_cast<int>(std::size(kSeaStatePresets));

/// @brief Jerlov の水型（吸収・散乱係数 [1/m]。外洋 I〜III、沿岸 1C〜9C の順に濁る）
struct JerlovWaterType {
	const char* name;
	Vector3 absorption;
	Vector3 scattering;
};

const JerlovWaterType kJerlovWaterTypes[] = {
	{ "I（熱帯の外洋・沖縄）", { 0.35f, 0.07f, 0.02f }, { 0.003f, 0.008f, 0.016f } },
	{ "IB（澄んだ外洋）",      { 0.38f, 0.09f, 0.04f }, { 0.006f, 0.012f, 0.020f } },
	{ "II（外洋）",            { 0.45f, 0.13f, 0.09f }, { 0.010f, 0.018f, 0.026f } },
	{ "III（沿岸寄りの外洋）", { 0.52f, 0.18f, 0.15f }, { 0.018f, 0.028f, 0.035f } },
	{ "1C（澄んだ沿岸）",      { 0.55f, 0.17f, 0.20f }, { 0.030f, 0.045f, 0.045f } },
	{ "5C（沿岸）",            { 0.65f, 0.30f, 0.45f }, { 0.080f, 0.100f, 0.085f } },
	{ "9C（濁った沿岸）",      { 0.85f, 0.50f, 0.90f }, { 0.180f, 0.200f, 0.150f } },
};
constexpr int kJerlovWaterTypeCount = static_cast<int>(std::size(kJerlovWaterTypes));

/// Gerstner の波のプリセットの名前（WaterPresetType の順）
constexpr const char* kGerstnerPresetNames[] = {
	"湖（静かな水）",
	"海（波が立つ水）",
	"池・プール",
	"雨水・水たまり",
};
constexpr int kGerstnerPresetCount = static_cast<int>(std::size(kGerstnerPresetNames));

/// コースティクスの作り方の名前（r.Water.Caustics.Backend の値の順）
constexpr const char* kCausticsBackendNames[] = {
	"レイトレーシング",
	"スクリーンスペース（Gerstner 用）",
};

/// @brief 値がほぼ同じか（差が大きい方の値の 1% 以内）
bool NearlyEqual(float lhs, float rhs) {
	return std::fabs(lhs - rhs) <= 0.01f * (std::max)({ 1.0f, std::fabs(lhs), std::fabs(rhs) });
}

bool NearlyEqual(const Vector3& lhs, const Vector3& rhs) {
	return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y) && NearlyEqual(lhs.z, rhs.z);
}

/// @brief 今の水とシーンの風に一致する海況のプリセット（無ければ -1）
int FindSeaStatePreset(const WaterProfile& profile) {
	for (int i = 0; i < kSeaStatePresetCount; ++i) {
		const SeaStatePreset& preset = kSeaStatePresets[i];
		if (NearlyEqual(WindCVars::Speed.Get(), preset.windSpeed)
			&& NearlyEqual(profile.fetchKm, preset.fetchKm)
			&& NearlyEqual(profile.amplitudeScale, preset.amplitudeScale)
			&& NearlyEqual(profile.choppiness, preset.choppiness)
			&& profile.componentCount == preset.componentCount
			&& profile.swellEnabled == preset.swellEnabled
			&& NearlyEqual(profile.swellHeight, preset.swellHeight)
			&& NearlyEqual(profile.swellPeriod, preset.swellPeriod)) {
			return i;
		}
	}
	return -1;
}

/// @brief 今の吸収・散乱係数に一致する水型（無ければ -1）
int FindJerlovWaterType(const WaterProfile& profile) {
	for (int i = 0; i < kJerlovWaterTypeCount; ++i) {
		if (NearlyEqual(profile.absorption, kJerlovWaterTypes[i].absorption)
			&& NearlyEqual(profile.scattering, kJerlovWaterTypes[i].scattering)
			&& profile.turbidity == 0.0f) {
			return i;
		}
	}
	return -1;
}

/// @brief CVar へ値を入れ、Undo を 1 件積む（BatchScope の中なら 1 回の Ctrl+Z にまとまる）
template <class T>
void AssignWithUndo(CVar<T>& cvar, const T& value) {
	auto& undoStack = CVarUndoStack::Get();
	undoStack.BeginEdit(&cvar);
	cvar.Set(value);
	undoStack.CommitEdit(&cvar);
	CVarRegistry::Get().NotifyCommit();
}

/// @brief 編集できない値の行（左にラベル、右に淡い文字）
void InfoRow(const char* label, const std::string& value) {
	InspectorLayout::BeginRow(label, Editor::Theme::kTextDim);
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", value.c_str());
}

/// @brief 一覧から選ぶ行（保存する値でない選択。選んだ番号を返し、選ばなければ -1）
/// @param current 今の番号（-1 なら unmatchedName を出す）
/// @param descriptions 項目ごとの説明（nullptr なら出さない）
int ChoiceRow(const char* id, const char* label, int current, const char* const* names,
	const char* const* descriptions, int count, const char* unmatchedName = "カスタム") {
	ImGui::PushID(id);
	InspectorLayout::BeginRow(label, Editor::Theme::kTextDim);
	int chosen = -1;
	if (ImGui::BeginCombo("##choice", current >= 0 ? names[current] : unmatchedName)) {
		for (int i = 0; i < count; ++i) {
			const bool selected = i == current;
			if (ImGui::Selectable(names[i], selected)) {
				chosen = i;
			}
			if (descriptions && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", descriptions[i]);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopID();
	return chosen;
}

/// @brief XZ の向きを、主の向きから角度 radians だけ回して返す
Vector2 RotateDirection(const Vector2& direction, float radians) {
	const float c = std::cos(radians);
	const float s = std::sin(radians);
	return { direction.x * c - direction.y * s, direction.x * s + direction.y * c };
}

/// @brief XZ の向きを長さ 1 にする（長さ 0 なら +X）
Vector2 NormalizeDirection(const Vector2& direction) {
	const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
	return length > 1.0e-5f ? Vector2{ direction.x / length, direction.y / length } : Vector2{ 1.0f, 0.0f };
}

/// @brief ファイル名に使えない文字を _ にする
std::string ToFileName(std::string name) {
	for (char& c : name) {
		if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}
	return name.empty() ? std::string("Water") : name;
}
}

bool WaterSurfaceParameterPanel::Target::Rows(std::initializer_list<const char*> names) const {
	return InspectorRenderer::DrawProperties(type, instance, context, names);
}

bool WaterSurfaceParameterPanel::Target::Assign(const json& values, const std::string& label) const {
	return InspectorRenderer::Assign(type, instance, values, context, label);
}

void WaterSurfaceParameterPanel::Initialize(WaterSystem& runtimeController, [[maybe_unused]] WaterEditorFacade& editorFacade) {
	gerstnerPreset_ = -1;
	if (const WaterSurfaceComponent* waterPlane = runtimeController.GetWaterPlane()) {
		gerstnerWaveCount_ = static_cast<int>(waterPlane->GetActiveWaveCount());
	}
}

bool WaterSurfaceParameterPanel::Draw(WaterSurfaceComponent& water, const InspectorRenderer::DrawContext& context,
	WaterSystem* runtimeController, WaterEditorFacade& editorFacade) {
	const Reflection::TypeDescriptor* const type = water.GetTypeDescriptor();
	void* const instance = water.GetReflectionInstance();
	if (!type || !instance) {
		return false;
	}
	const Target target{ water, *type, instance, context };

	bool changed = target.Rows({ "size", "resolution", "tideAmplitude", "tideHeight", "waterLevel" });
	changed = DrawProfileSection(target) || changed;
	changed = DrawWaveSection(target, editorFacade) || changed;
	changed = DrawWaterColorSection(target) || changed;
	changed = DrawSurfaceSection(target) || changed;
	changed = DrawFoamSection(target, runtimeController) || changed;
	DrawWetnessSection();
	DrawCausticsSection(target);
	return changed;
}

bool WaterSurfaceParameterPanel::DrawProfileSection(const Target& target) {
	if (!InspectorLayout::Foldout("プロファイル", true)) {
		return false;
	}

	bool changed = target.Rows({ "profileAsset" });
	WaterSurfaceComponent& water = target.water;
	const AssetRef<WaterProfileAsset>& profileAsset = water.GetProfileAsset();
	if (profileAsset.IsSet()) {
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kWriteProfileLabel) + spacing + UI::Bar::ButtonWidth(kReloadProfileLabel));
		if (UI::Bar::Button(kWriteProfileLabel, false, "今の値をプロファイルのファイルへ書く")) {
			water.SaveProfileToAsset();
		}
		ImGui::SameLine();
		if (UI::Bar::Button(kReloadProfileLabel, false, "プロファイルの値を入れ直す（Ctrl+Z で戻せる）")) {
			changed = target.Assign(water.ReadProfileAsset(), "水面の値をプロファイルから読み直す") || changed;
		}
		InspectorLayout::Hint("シーンを開いたときと指し直したときに、プロファイルの値が入る。ここで変えた値をプロファイルにも残すには「プロファイルへ書き込む」。");
		return changed;
	}

	InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kCreateProfileLabel));
	if (UI::Bar::Button(kCreateProfileLabel, false, "今の値で Assets/Water に .waterprofile を作り、それを指す（ほかのシーンの水からも指せる）")) {
		const GameObject* const owner = water.GetOwner();
		const std::string baseName = ToFileName(owner ? owner->GetName() : std::string());
		std::string path = std::format("{}/{}.waterprofile", kProfileFolder, baseName);
		for (int n = 2; JsonManager::GetInstance().FileExists(path); ++n) {
			path = std::format("{}/{}_{}.waterprofile", kProfileFolder, baseName, n);
		}
		const AssetInfo* const info = water.WriteProfile(path)
			? AssetDatabase::GetInstance().ImportAsset(ProjectPaths::Resolve(path)) : nullptr;
		if (info) {
			const json values = { { "profileAsset",
				Reflection::PropertySerializer::AssetRefToJson(Reflection::AssetRefValue{ info->guid, ToAssetPath(*info) }) } };
			changed = target.Assign(values, "水面のプロファイルを作って指す") || changed;
		}
	}
	InspectorLayout::Hint("水の性質（波・水の色・反射と屈折・泡）はこの水面ごとにシーンへ保存する。プロファイルにすると、ほかのシーンの水と同じ値を使える。");
	return changed;
}

bool WaterSurfaceParameterPanel::DrawWaveSection(const Target& target, WaterEditorFacade& editorFacade) {
	if (!InspectorLayout::Foldout("波", true)) {
		return false;
	}

	bool changed = target.Rows({ "fftEnabled" });
	if (!target.water.GetProfile().fftEnabled) {
		DrawGerstnerRows(target.water);
		return changed;
	}

	changed = DrawSeaStateRow(target) || changed;
	DrawWaveSummaryRows(editorFacade);

	ImGui::SeparatorText("風（シーンの設定）");
	CVarUI::DrawRow(&WindCVars::Speed, "風速", "%.1f m/s");
	CVarUI::DrawDirectionRow(&WindCVars::Direction, "風が吹いていく向き");
	InspectorLayout::Hint("風はシーンの _environment.json に保存する。植物の揺れにも効く。");

	ImGui::SeparatorText("風波");
	changed = target.Rows({ "fetchKm", "amplitudeScale", "choppiness", "componentCount" }) || changed;

	ImGui::SeparatorText("うねり");
	changed = target.Rows({ "swellEnabled" }) || changed;
	ImGui::BeginDisabled(!target.water.GetProfile().swellEnabled);
	changed = target.Rows({ "swellHeight", "swellPeriod", "swellDirection", "swellRelativeWidth", "swellSpread" }) || changed;
	ImGui::EndDisabled();

	ImGui::SeparatorText("形（プロジェクトの設定）");
	CVarUI::DrawRow(&WaterCVars::ShoreFineMesh, "カメラの近くを細かく描く");
	CVarUI::DrawRow(&WaterCVars::SeaMapEnabled, "陸と浅瀬で波を弱める");
	ImGui::BeginDisabled(!WaterCVars::SeaMapEnabled.Get());
	CVarUI::DrawRow(&WaterCVars::SeaMapResolution, "海の範囲の細かさ");
	ImGui::EndDisabled();
	return changed;
}

bool WaterSurfaceParameterPanel::DrawSeaStateRow(const Target& target) {
	static const char* names[kSeaStatePresetCount];
	static const char* descriptions[kSeaStatePresetCount];
	for (int i = 0; i < kSeaStatePresetCount; ++i) {
		names[i] = kSeaStatePresets[i].name;
		descriptions[i] = kSeaStatePresets[i].description;
	}
	const int chosen = ChoiceRow("seaState", "海況", FindSeaStatePreset(target.water.GetProfile()), names, descriptions, kSeaStatePresetCount);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("シーンの風速と、吹送距離・うねりなどをまとめて入れる（Ctrl+Z で戻せる）。\n風の向きとうねりの向きは変えない");
	}
	if (chosen < 0) {
		return false;
	}

	const SeaStatePreset& preset = kSeaStatePresets[chosen];
	const std::string label = std::format("海況を「{}」にする", preset.name);
	Editor::BatchScope batch(label);
	const json values = {
		{ "fetchKm", preset.fetchKm },
		{ "amplitudeScale", preset.amplitudeScale },
		{ "choppiness", preset.choppiness },
		{ "componentCount", preset.componentCount },
		{ "swellEnabled", preset.swellEnabled },
		{ "swellHeight", preset.swellHeight },
		{ "swellPeriod", preset.swellPeriod },
	};
	const bool changed = target.Assign(values, label);
	AssignWithUndo(WindCVars::Speed, preset.windSpeed);
	return changed;
}

void WaterSurfaceParameterPanel::DrawWaveSummaryRows(WaterEditorFacade& editorFacade) {
	const FFTOceanManager* manager = editorFacade.GetFFTOceanManager();
	if (!manager || !manager->IsInitialized()) {
		InfoRow("出来上がる波", "FFT の海を準備中");
		return;
	}
	const FFTOceanSpectrum::WaveSummary summary = manager->GetSpectrum().GetWaveSummary();
	InfoRow("風波の大きさ", std::format("波長 {:.1f} m・高さ {:.2f} m", summary.windSeaWavelength, summary.windSeaHeight));
	InfoRow("うねりの大きさ", summary.swellHeight > 0.0f
		? std::format("波長 {:.0f} m・高さ {:.2f} m", summary.swellWavelength, summary.swellHeight)
		: std::string("なし"));
}

void WaterSurfaceParameterPanel::DrawGerstnerRows(WaterSurfaceComponent& water) {
	const int chosen = ChoiceRow("gerstnerPreset", "波の種類", gerstnerPreset_, kGerstnerPresetNames, nullptr,
		kGerstnerPresetCount, "既定の波");
	if (chosen >= 0) {
		gerstnerPreset_ = chosen;
		gerstnerWaveCount_ = static_cast<int>(GetWaterPresetData(static_cast<WaterPresetType>(gerstnerPreset_)).recommendedWaveCount);
		RegenerateGerstnerWaves(water);
	}

	InspectorLayout::BeginRow("波の本数", Editor::Theme::kTextDim);
	const int maxWaveCount = static_cast<int>(kMaxWaterWaveCount);
	const bool countChanged = InspectorLayout::FilledField(
		static_cast<float>(gerstnerWaveCount_ - 1) / static_cast<float>((std::max)(maxWaveCount - 1, 1)), [&] {
			return ImGui::DragInt("##gerstnerWaveCount", &gerstnerWaveCount_, 0.1f, 1, maxWaveCount, "%d",
				ImGuiSliderFlags_AlwaysClamp);
		});
	if (countChanged) {
		if (gerstnerPreset_ >= 0) {
			RegenerateGerstnerWaves(water);
		} else {
			water.SetActiveWaveCount(static_cast<uint32_t>(gerstnerWaveCount_));
		}
	}

	if (gerstnerPreset_ >= 0) {
		InspectorLayout::AlignToRight(UI::Bar::ButtonWidth("波を作り直す"));
		if (UI::Bar::Button("波を作り直す", false, "同じ種類で、向きと位相を振り直す")) {
			RegenerateGerstnerWaves(water);
		}
	}
	InspectorLayout::Hint("Gerstner の波は保存しません。開き直すと既定の波に戻ります。");

	if (!ImGui::TreeNode("個別の波")) {
		return;
	}
	WaveParams* waves = water.GetWaves();
	const uint32_t waveCount = (std::min)(water.GetActiveWaveCount(), kMaxWaterWaveCount);
	for (uint32_t waveIndex = 0; waveIndex < waveCount; ++waveIndex) {
		ImGui::PushID(static_cast<int>(waveIndex));
		if (ImGui::TreeNode("##wave", "波 %u", waveIndex)) {
			WaveParams& wave = waves[waveIndex];
			InspectorLayout::BeginRow("進む向き", Editor::Theme::kTextDim);
			if (InspectorLayout::VectorField("##direction", &wave.direction.x, 2, 0.01f, -1.0f, 1.0f)) {
				wave.direction = NormalizeDirection(wave.direction);
			}
			InspectorLayout::BeginRow("振幅", Editor::Theme::kTextDim);
			ImGui::DragFloat("##amplitude", &wave.amplitude, 0.01f, 0.0f, 5.0f, "%.2f m");
			InspectorLayout::BeginRow("波長", Editor::Theme::kTextDim);
			ImGui::DragFloat("##wavelength", &wave.wavelength, 0.1f, 0.1f, 50.0f, "%.1f m");
			InspectorLayout::BeginRow("速さ", Editor::Theme::kTextDim);
			ImGui::DragFloat("##speed", &wave.speed, 0.05f, 0.0f, 10.0f, "%.2f");
			InspectorLayout::BeginRow("尖り", Editor::Theme::kTextDim);
			ImGui::DragFloat("##steepness", &wave.steepness, 0.01f, 0.0f, 1.0f, "%.2f");
			InspectorLayout::BeginRow("位相", Editor::Theme::kTextDim);
			ImGui::DragFloat("##phase", &wave.phaseOffset, 0.05f, -kTwoPi * 2.0f, kTwoPi * 2.0f, "%.2f");
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	ImGui::TreePop();
}

void WaterSurfaceParameterPanel::RegenerateGerstnerWaves(WaterSurfaceComponent& water) {
	const WaterPresetData& presetData = GetWaterPresetData(static_cast<WaterPresetType>(gerstnerPreset_));
	const uint32_t activeWaveCount = static_cast<uint32_t>(std::clamp(gerstnerWaveCount_, 1, static_cast<int>(kMaxWaterWaveCount)));
	auto& random = RandomGenerator::GetInstance();

	// プリセットのレイヤーごとの本数を、今の本数に収まるよう配る（足りない分は順に足す）
	uint32_t layerCounts[kWaterWaveLayerCount] = {};
	uint32_t assignedCount = 0;
	for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount && assignedCount < activeWaveCount; ++layerIndex) {
		layerCounts[layerIndex] = (std::min)(presetData.layers[layerIndex].count, activeWaveCount - assignedCount);
		assignedCount += layerCounts[layerIndex];
	}
	for (uint32_t fillLayer = 0; assignedCount < activeWaveCount; ++fillLayer, ++assignedCount) {
		++layerCounts[fillLayer % kWaterWaveLayerCount];
	}

	// 主の向き・向きのばらつき・レイヤーごとの範囲から波を作る
	const Vector2 dominantDirection = NormalizeDirection(presetData.dominantDirection);
	uint32_t waveIndex = 0;
	for (uint32_t layerIndex = 0; layerIndex < kWaterWaveLayerCount; ++layerIndex) {
		const WaterWaveLayerConfig& layer = presetData.layers[layerIndex];
		const uint32_t countInLayer = layerCounts[layerIndex];
		for (uint32_t layerWaveIndex = 0; layerWaveIndex < countInLayer && waveIndex < activeWaveCount; ++layerWaveIndex, ++waveIndex) {
			const float baseAngle = (countInLayer > 1)
				? (-0.5f + static_cast<float>(layerWaveIndex) / static_cast<float>(countInLayer - 1)) * layer.directionSpreadRadians
				: 0.0f;
			const float jitter = random.GetFloat(-presetData.directionJitterRadians, presetData.directionJitterRadians);

			WaveParams wave{};
			wave.direction = NormalizeDirection(RotateDirection(dominantDirection, baseAngle + jitter));
			wave.amplitude = random.GetFloat(layer.amplitudeMin, layer.amplitudeMax);
			wave.wavelength = random.GetFloat(layer.wavelengthMin, layer.wavelengthMax);
			wave.speed = random.GetFloat(layer.speedMin, layer.speedMax);
			wave.steepness = random.GetFloat(layer.steepnessMin, layer.steepnessMax);
			wave.phaseOffset = random.GetFloat(0.0f, kTwoPi);
			water.SetWave(waveIndex, wave);
		}
	}

	// 使わない枠は主の向きだけ残して 0 にする
	for (; waveIndex < kMaxWaterWaveCount; ++waveIndex) {
		WaveParams wave{};
		wave.direction = dominantDirection;
		water.SetWave(waveIndex, wave);
	}

	water.SetActiveWaveCount(activeWaveCount);
}

bool WaterSurfaceParameterPanel::DrawWaterColorSection(const Target& target) {
	if (!InspectorLayout::Foldout("水の色", true)) {
		return false;
	}

	static const char* names[kJerlovWaterTypeCount];
	for (int i = 0; i < kJerlovWaterTypeCount; ++i) {
		names[i] = kJerlovWaterTypes[i].name;
	}
	bool changed = false;
	const int chosen = ChoiceRow("waterType", "水質", FindJerlovWaterType(target.water.GetProfile()), names, nullptr, kJerlovWaterTypeCount);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Jerlov の水型。吸収と散乱をまとめて入れ、濁りを 0 に戻す（Ctrl+Z で戻せる）");
	}
	if (chosen >= 0) {
		const JerlovWaterType& type = kJerlovWaterTypes[chosen];
		const json values = {
			{ "absorption", JsonManager::Vector3ToJson(type.absorption) },
			{ "scattering", JsonManager::Vector3ToJson(type.scattering) },
			{ "turbidity", 0.0f },
		};
		changed = target.Assign(values, std::format("水質を「{}」にする", type.name));
	}

	changed = target.Rows({ "absorption", "scattering", "turbidity", "depthFadeEnabled" }) || changed;
	InspectorLayout::Hint("水の色は吸収と散乱から光で決まる。澄んだ水は 吸収が 赤 > 緑 > 青。");
	return changed;
}

bool WaterSurfaceParameterPanel::DrawSurfaceSection(const Target& target) {
	if (!InspectorLayout::Foldout("反射と屈折", false)) {
		return false;
	}
	const bool changed = target.Rows({ "fresnelScale", "fresnelF0", "roughness", "refractiveIndex", "baseColor" });
	CVarUI::DrawRow(&WaterCVars::RefractionMaxOffsetPixels, "屈折のずれの上限（プロジェクト）", "%.0f px");
	return changed;
}

bool WaterSurfaceParameterPanel::DrawFoamSection(const Target& target, WaterSystem* runtimeController) {
	if (!InspectorLayout::Foldout("泡", false)) {
		return false;
	}
	const WaterProfile& profile = target.water.GetProfile();
	if (!profile.fftEnabled) {
		InspectorLayout::HelpBox("泡は FFT の海のときだけ出ます。");
	}

	bool changed = target.Rows({ "foamEnabled" });
	ImGui::BeginDisabled(!target.water.GetProfile().foamEnabled);
	changed = target.Rows({ "foamWhitecapScale" }) || changed;
	if (const WaterWhitecapFoam* foam = runtimeController ? runtimeController->GetWaterWhitecapFoam() : nullptr) {
		InfoRow("白波の割合", std::format("目標 {:.2f}% ／ 実測 {:.2f}%",
			foam->GetTargetCoverage() * 100.0f, foam->GetMeasuredCoverage() * 100.0f));
		InfoRow("実測の内訳", std::format("砕けている最中 {:.2f}% ／ 残り {:.2f}%",
			foam->GetMeasuredInstantCoverage() * 100.0f, foam->GetMeasuredResidueCoverage() * 100.0f));
	}
	changed = target.Rows({ "foamOpacity", "foamDecaySeconds", "foamCascadeWeights" }) || changed;
	ImGui::EndDisabled();
	InspectorLayout::Hint("白波の割合はシーンの風速から観測式で決まる。白波の量はその倍率。");
	return changed;
}

void WaterSurfaceParameterPanel::DrawWetnessSection() {
	if (!InspectorLayout::Foldout("濡れ跡（プロジェクトの設定）", false)) {
		return;
	}
	CVarUI::DrawRow(&WaterCVars::WetnessEnabled, "水が引いたあとを濡らす");
	ImGui::BeginDisabled(!WaterCVars::WetnessEnabled.Get());
	CVarUI::DrawRow(&WaterCVars::WetnessDrySeconds, "乾くまでの時間", "%.0f s");
	ImGui::EndDisabled();
	InspectorLayout::Hint("潮などで水面が下がると、水に浸かっていた面が暗く、つやのある見た目で残り、時間とともに乾く。");
}

void WaterSurfaceParameterPanel::DrawCausticsSection(const Target& target) {
	if (!InspectorLayout::Foldout("コースティクス（プロジェクトの設定）", false)) {
		return;
	}
	CVarUI::DrawEnumRow(&WaterCVars::CausticsBackend, "作り方", kCausticsBackendNames,
		static_cast<int>(std::size(kCausticsBackendNames)));
	if (WaterCVars::CausticsBackend.Get() != 1) {
		InspectorLayout::Hint("レイトレーシングの明るさは、光と水の吸収から決まる。屈折率は「反射と屈折」の水の値を使う。");
		return;
	}
	if (target.water.GetProfile().fftEnabled) {
		InspectorLayout::HelpBox("スクリーンスペース方式は Gerstner の波のときだけ出ます。", InspectorLayout::Message::Warning);
	}
	CVarUI::DrawRow(&WaterCVars::CausticsIntensity, "強さ", "%.2f");
	CVarUI::DrawRow(&WaterCVars::CausticsDepthAttenuation, "深さによる弱まり", "%.2f");
	CVarUI::DrawRow(&WaterCVars::CausticsCurvatureScale, "曲率の倍率", "%.2f");
	CVarUI::DrawRow(&WaterCVars::CausticsSurfaceSampleRadius, "水面を調べる半径", "%.2f m");
	CVarUI::DrawRow(&WaterCVars::CausticsReceiverNormalStrength, "受ける面の向きの効き", "%.2f");
	CVarUI::DrawRow(&WaterCVars::CausticsAlignmentPower, "集まり方の鋭さ", "%.1f");
}

#endif
