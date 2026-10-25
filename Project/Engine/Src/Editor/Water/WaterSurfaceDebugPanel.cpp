#include "pch.h"
#include "Editor/Water/WaterSurfaceDebugPanel.h"

#ifdef CORE_EDITOR

#include "Graphics/Water/Render/WaterRenderFeature.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/Inspector/InspectorLayout.h"

#include "../../../Assets/Shaders/Water/Common/FFTOceanCascadeValues.hlsli"

#include <format>
#include <string>

using namespace CoreEngine;

namespace {
/// @brief 一覧に出す表示モード（モードの値と名前）
struct ViewModeItem {
	int mode;
	const char* name;
};

/// @brief 水面の可視化の項目（モード名は kWaterDebugViewModeNames を使う）
ViewModeItem SurfaceView(WaterDebugViewMode mode) {
	return { static_cast<int>(mode), kWaterDebugViewModeNames[static_cast<int>(mode)] };
}

/// 水面の可視化で選べるモード（「なし」は「可視化する」の入り切りで代え、何も映らないモードは出さない）
const ViewModeItem kSurfaceViews[] = {
	SurfaceView(WaterDebugViewMode::RawDepth),
	SurfaceView(WaterDebugViewMode::LinearDepth),
	SurfaceView(WaterDebugViewMode::DepthDelta),
	SurfaceView(WaterDebugViewMode::ScreenUV),
	SurfaceView(WaterDebugViewMode::SceneColor),
	SurfaceView(WaterDebugViewMode::Reflection),
	SurfaceView(WaterDebugViewMode::Fresnel),
	SurfaceView(WaterDebugViewMode::Reflectance),
	SurfaceView(WaterDebugViewMode::Transmission),
	SurfaceView(WaterDebugViewMode::Transmittance),
	SurfaceView(WaterDebugViewMode::WaterComposite),
	SurfaceView(WaterDebugViewMode::CompositeTransmissionOnly),
	SurfaceView(WaterDebugViewMode::CompositeReflectionOnly),
	SurfaceView(WaterDebugViewMode::ReflectionMinusTransmission),
	SurfaceView(WaterDebugViewMode::PlanarReflectionRaw),
	SurfaceView(WaterDebugViewMode::SkyEnvCloudColor),
	SurfaceView(WaterDebugViewMode::RTRefraction),
	SurfaceView(WaterDebugViewMode::RTRefractionReason),
	SurfaceView(WaterDebugViewMode::RTRefractionVsScene),
	SurfaceView(WaterDebugViewMode::RTRefractionSuccessMask),
	SurfaceView(WaterDebugViewMode::FFTOceanJacobian),
	SurfaceView(WaterDebugViewMode::FFTOceanFoam),
};

constexpr ViewModeItem kRefractionViews[] = {
	{ 0, "なし" },
	{ 1, "UV のずれ (px)" },
	{ 2, "深度の食い違い" },
	{ 3, "水面の法線" },
	{ 4, "屈折の向き" },
};

constexpr ViewModeItem kCausticsViews[] = {
	{ 0, "合成結果" },
	{ 1, "そのままの RGB" },
	{ 2, "グレースケール" },
	{ 3, "水中の判定 (R=置換率 / G=矩形 / B=水深)" },
	{ 4, "水面の線のつながり (緑=つながる / 赤=明るすぎ)" },
};

/// レイトレのコースティクスの表示（1 の浅瀬のフェードは何も映らないので出さない）
constexpr ViewModeItem kRTCausticsViews[] = {
	{ 0, "なし" },
	{ 2, "一致の度合い" },
	{ 3, "透過率 (Beer-Lambert)" },
	{ 4, "受ける面の幾何項" },
	{ 5, "最終の強さ" },
	{ 6, "集光率 (ヤコビアン)" },
};

/// @brief 倍率が効く水面の可視化か（生の深度・線形深度・深度差）
bool SurfaceViewUsesScale(int mode) {
	return mode == static_cast<int>(WaterDebugViewMode::RawDepth)
		|| mode == static_cast<int>(WaterDebugViewMode::LinearDepth)
		|| mode == static_cast<int>(WaterDebugViewMode::DepthDelta);
}

/// @brief 倍率が効くレイトレの屈折の表示か（UV のずれ・深度の食い違い）
bool RefractionViewUsesScale(int mode) {
	return mode == 1 || mode == 2;
}

/// @brief 編集できない値の行
void InfoRow(const char* label, const std::string& value) {
	InspectorLayout::BeginRow(label, Editor::Theme::kTextDim);
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", value.c_str());
}

/// @brief 表示モードを一覧から選ぶ行（一覧に無い値なら先頭の名前を出す）
template <size_t N>
bool ViewModeRow(const char* id, const char* label, int& mode, const ViewModeItem (&items)[N]) {
	ImGui::PushID(id);
	InspectorLayout::BeginRow(label, Editor::Theme::kTextDim);
	const char* preview = items[0].name;
	for (const ViewModeItem& item : items) {
		if (item.mode == mode) {
			preview = item.name;
		}
	}
	bool changed = false;
	if (ImGui::BeginCombo("##value", preview)) {
		for (const ViewModeItem& item : items) {
			const bool selected = item.mode == mode;
			if (ImGui::Selectable(item.name, selected) && !selected) {
				mode = item.mode;
				changed = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::PopID();
	return changed;
}

/// @brief 倍率の行
bool ScaleRow(const char* id, const char* label, float& value) {
	ImGui::PushID(id);
	InspectorLayout::BeginRow(label, Editor::Theme::kTextDim);
	const bool changed = ImGui::DragFloat("##value", &value, 0.02f, 0.1f, 16.0f, "×%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::PopID();
	return changed;
}

/// @brief 入り切りの行
bool CheckRow(const char* id, const char* label, bool& value) {
	ImGui::PushID(id);
	InspectorLayout::BeginRow(label, Editor::Theme::kTextDim);
	const bool changed = ImGui::Checkbox("##value", &value);
	ImGui::PopID();
	return changed;
}
}

void WaterSurfaceDebugPanel::Initialize(WaterRenderFeature& runtimeController) {
	if (WaterSurfaceComponent* waterPlane = runtimeController.GetWaterPlane()) {
		waterPlane->SetDepthFadeDebug(surfaceViewEnabled_, surfaceViewScale_);
		waterPlane->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(surfaceViewMode_));
	}
}

void WaterSurfaceDebugPanel::Draw(WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade) {
	if (!runtimeController.GetWaterPlane() || !InspectorLayout::Foldout("デバッグ", false)) {
		return;
	}
	InspectorLayout::Hint("ここの値は保存しません。状態の確認と、描いた中身の可視化に使います。");
	DrawStatusRows(runtimeController, editorFacade);
	DrawSurfaceView(runtimeController);
	DrawRefractionDebug(editorFacade);
	DrawCausticsDebug(editorFacade);
}

void WaterSurfaceDebugPanel::DrawStatusRows(WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade) {
	const WaterSurfaceComponent* waterPlane = runtimeController.GetWaterPlane();
	ImGui::SeparatorText("状態");
	InfoRow("描き方", waterPlane->IsUsingFFTOcean() ? "FFT の海" : "Gerstner の波");
	if (waterPlane->IsUsingFFTOcean()) {
		const FFTOceanManager* manager = editorFacade.GetFFTOceanManager();
		const bool ready = manager && manager->IsInitialized() && waterPlane->HasFFTOceanTextureSRVs();
		InfoRow("FFT の海", ready
			? std::format("準備できた（{0}×{0}）", manager->GetSettings().resolution)
			: std::string("準備中"));
		constexpr float kPatchLengths[] = FFT_OCEAN_CASCADE_PATCH_LENGTHS;
		InfoRow("パッチの大きさ", std::format("{:.0f} / {:.0f} / {:.0f} m", kPatchLengths[0], kPatchLengths[1], kPatchLengths[2]));
	}
	InfoRow("水面の高さ", std::format("{:.2f} m", waterPlane->GetTransform().GetTranslate().y));
	InfoRow("反射", waterPlane->GetFrameConstants().reflectionEnabled != 0 ? "使う" : "使わない");
}

void WaterSurfaceDebugPanel::DrawSurfaceView(WaterRenderFeature& runtimeController) {
	WaterSurfaceComponent* waterPlane = runtimeController.GetWaterPlane();
	ImGui::SeparatorText("水面の可視化");
	bool changed = CheckRow("surfaceView", "可視化する", surfaceViewEnabled_);
	ImGui::BeginDisabled(!surfaceViewEnabled_);
	changed |= ViewModeRow("surfaceMode", "表示", surfaceViewMode_, kSurfaceViews);
	if (SurfaceViewUsesScale(surfaceViewMode_)) {
		changed |= ScaleRow("surfaceScale", "倍率", surfaceViewScale_);
	}
	ImGui::EndDisabled();
	if (changed) {
		waterPlane->SetDepthFadeDebug(surfaceViewEnabled_, surfaceViewScale_);
		waterPlane->SetDepthDebugViewMode(static_cast<WaterDebugViewMode>(surfaceViewMode_));
	}
}

void WaterSurfaceDebugPanel::DrawRefractionDebug(WaterEditorFacade& editorFacade) {
	WaterEditorRefractionDebug debug = editorFacade.GetRefractionDebug();
	if (!debug.available) {
		return;
	}
	ImGui::SeparatorText("レイトレの屈折");
	bool changed = ViewModeRow("refractionMode", "表示", debug.viewMode, kRefractionViews);
	if (RefractionViewUsesScale(debug.viewMode)) {
		changed |= ScaleRow("refractionScale", "倍率", debug.displayScale);
	}
	changed |= CheckRow("refractionLog", "ログに出す", debug.logEnabled);
	if (changed) {
		editorFacade.ApplyRefractionDebug(debug);
	}
	InspectorLayout::Hint("水面の可視化の表示を「RT屈折」か「RT屈折とシーン比較」にすると見比べやすい。");
}

void WaterSurfaceDebugPanel::DrawCausticsDebug(WaterEditorFacade& editorFacade) {
	WaterEditorCausticsDebug debug = editorFacade.GetCausticsDebug();
	if (!debug.techniqueAvailable && !debug.rtManagerAvailable) {
		return;
	}
	ImGui::SeparatorText("コースティクス");
	bool changed = false;
	if (debug.techniqueAvailable) {
		changed |= ViewModeRow("causticsMode", "表示", debug.viewMode, kCausticsViews);
		changed |= ScaleRow("causticsScale", "倍率", debug.displayScale);
		changed |= CheckRow("causticsLog", "ログに出す", debug.logEnabled);
	}
	if (debug.rtManagerAvailable) {
		changed |= ViewModeRow("rtCausticsMode", "レイトレの表示", debug.rtViewMode, kRTCausticsViews);
		if (debug.rtViewMode != 0) {
			changed |= ScaleRow("rtCausticsScale", "レイトレの倍率", debug.rtDisplayScale);
		}
		changed |= CheckRow("rtCausticsLog", "レイトレのログ", debug.rtLogEnabled);
	}
	if (changed) {
		editorFacade.ApplyCausticsDebug(debug);
	}
}

#endif
