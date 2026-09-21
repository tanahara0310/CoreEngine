#include "pch.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/Water/WaterEditorPanel.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Water/Render/WaterRenderFeature.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#endif

using namespace CoreEngine;

#ifdef CORE_EDITOR
namespace {
	constexpr const char* kEditorLabel = "Water";
}
#endif

namespace CoreEngine {

WaterEditorPanel::~WaterEditorPanel() {
	Shutdown();
}

void WaterEditorPanel::Shutdown() {
	waterFeature_ = nullptr;
#ifdef CORE_EDITOR
	// シーン破棄後にドロワーがダングリングしないよう登録を解除する
	// （パラメータの永続化は CVars.json が担うため、ここで保存処理は不要）
	if (engine_) {
		Editor::EditorPanelRegistry::Get().Unregister(kEditorLabel, this);
		// 解除済みなので、保険で呼ばれるデストラクタ側では何もしない
		engine_ = nullptr;
	}
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
	engine_ = &engine;
	// UI は facade 経由で Water 設定を取得・適用する
	editorFacade_.Initialize(*waterFeature_, engine);
	// 通常パラメータ編集とデバッグ表示の各パネルを初期化する
	parameterPanel_.Initialize(*waterFeature_, editorFacade_);
	debugPanel_.Initialize(*waterFeature_);

	// Hierarchy の Environment ツリーへ登録し、選択時に Inspector で編集できるようにする
	Editor::EditorPanelRegistry::Get().Register({
		.id = kEditorLabel,
		.placement = Editor::PanelPlacement::EnvironmentTree,
		.owner = this,
		.draw = [this]() { DrawImGuiContent(); },
		});

	// パラメータの復元・保存は WaterCVars（CVars.json / CVarSettingsSection）が担う。
	// 旧 WaterSettingsSection（Water.json）は Phase 5 で廃止した
#endif
}

#ifdef CORE_EDITOR
void WaterEditorPanel::DrawImGuiContent() {
	if (!waterFeature_ || !waterFeature_->GetWaterPlane()) {
		ImGui::TextDisabled("水面オブジェクトがありません");
		return;
	}

	// 見た目調整とデバッグ機能を用途別に分けて表示する
	ImGui::TextDisabled("見た目調整とデバッグ表示を分けて管理します。");
	ImGui::Spacing();

	parameterPanel_.Draw(*waterFeature_, editorFacade_);
	debugPanel_.Draw(*waterFeature_, editorFacade_);
}
#endif

}
