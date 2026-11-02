#include "pch.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Water/WaterEditorPanel.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterSystem.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Inspector/InspectorRenderer.h"
#endif

using namespace CoreEngine;

#ifdef CORE_EDITOR
namespace {
	/// @brief この設定を持つコンポーネントの型名
	constexpr const char* kComponentTypeName = "WaterSurface";

	/// @brief プロジェクトの水の描画設定の CVar の接頭辞
	constexpr const char* kCVarPrefix = "r.Water.";

	/// @brief プロジェクトの水の描画設定を既定値へ戻すボタンの文字
	constexpr const char* kResetLabel = "プロジェクトの水の描画設定を既定値へ戻す";
}
#endif

namespace CoreEngine {

void WaterEditorPanel::Shutdown() {
	waterFeature_ = nullptr;
#ifdef CORE_EDITOR
	inspector_.Reset();
#endif
}

void WaterEditorPanel::Initialize(
	[[maybe_unused]] WaterSystem* waterFeature,
	[[maybe_unused]] EngineSystem& engine) {
	waterFeature_ = waterFeature;
	if (!waterFeature_) {
		return;
	}

#ifdef CORE_EDITOR
	// UI は facade 経由で Water 設定を取得・適用する
	editorFacade_.Initialize(*waterFeature_, engine);
	// 通常パラメータ編集とデバッグ表示の各パネルを初期化する
	parameterPanel_.Initialize(*waterFeature_, editorFacade_);
	debugPanel_.Initialize(*waterFeature_);

	// 水面コンポーネントの欄を見出しごとに分けて描き、後ろにデバッグ表示を足す
	inspector_ = Editor::ComponentInspectors::Register(kComponentTypeName, {
		.displayName = "水面",
		.drawProperties = [this](IComponent& component, const InspectorRenderer::DrawContext& context) {
			return DrawProperties(component, context);
		},
		.drawExtra = [this](IComponent& component) { DrawExtra(component); },
		});
#endif
}

#ifdef CORE_EDITOR
bool WaterEditorPanel::DrawProperties(IComponent& component, const InspectorRenderer::DrawContext& context) {
	return parameterPanel_.Draw(static_cast<WaterSurfaceComponent&>(component), context, waterFeature_, editorFacade_);
}

void WaterEditorPanel::DrawExtra(IComponent& component) {
	// デバッグ表示はシーンで描いている水面のときだけ出す
	if (waterFeature_ && waterFeature_->GetWaterPlane() == &component) {
		debugPanel_.Draw(*waterFeature_, editorFacade_);
	}

	ImGui::Spacing();
	InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kResetLabel));
	if (UI::Bar::Button(kResetLabel, false, "r.Water.*（描画の品質と負荷）をすべて既定値へ戻す（Ctrl+Z で戻せる）")) {
		CVarUI::ResetTree(kCVarPrefix);
	}
}
#endif

}
