#include "pch.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Water/WaterEditorPanel.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Water/Render/WaterRenderFeature.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/InspectorLayout.h"
#endif

using namespace CoreEngine;

#ifdef CORE_EDITOR
namespace {
	/// @brief この設定を持つコンポーネントの型名
	constexpr const char* kComponentTypeName = "WaterSurface";

	/// @brief 水面の CVar の接頭辞
	constexpr const char* kCVarPrefix = "r.Water.";

	/// @brief すべて既定値へ戻すボタンの文字
	constexpr const char* kResetLabel = "水面の設定をすべて既定値へ戻す";
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
	[[maybe_unused]] WaterRenderFeature* waterFeature,
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

	// シーンに置かれた水面コンポーネントのインスペクタとして中身を描く
	inspector_ = Editor::ComponentInspectors::Register(kComponentTypeName, {
		.displayName = "水面",
		.drawBody = [this](IComponent&) { DrawImGuiContent(); return false; },
		});
#endif
}

#ifdef CORE_EDITOR
void WaterEditorPanel::DrawImGuiContent() {
	if (!waterFeature_ || !waterFeature_->GetWaterPlane()) {
		UI::Hint("水面オブジェクトがありません");
		return;
	}

	ImGui::Spacing();
	InspectorLayout::HelpBox("ここから下の値はプロジェクト全体の設定（CVars.json）です。どのシーンの水面にも同じ値が効きます。");
	ImGui::Spacing();

	parameterPanel_.Draw(*waterFeature_, editorFacade_);
	debugPanel_.Draw(*waterFeature_, editorFacade_);

	ImGui::Spacing();
	InspectorLayout::AlignToRight(UI::Bar::ButtonWidth(kResetLabel));
	if (UI::Bar::Button(kResetLabel, false, "r.Water.* をすべて既定値へ戻す（Ctrl+Z で戻せる）")) {
		CVarUI::ResetTree(kCVarPrefix);
	}
}
#endif

}
