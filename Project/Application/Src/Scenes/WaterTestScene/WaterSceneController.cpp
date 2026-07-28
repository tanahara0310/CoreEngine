#include "pch.h"
#include "WaterSceneController.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Water/Render/WaterRenderFeature.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "EngineSystem/Settings/EditorSettingsSubsystem.h"
#endif

using namespace CoreEngine;

#ifdef USE_IMGUI
namespace {
	constexpr const char* kEditorLabel = "Water";
}
#endif

WaterSceneController::~WaterSceneController() {
	Shutdown();
}

void WaterSceneController::Shutdown() {
	waterFeature_ = nullptr;
#ifdef USE_IMGUI
	// シーン破棄後にドロワーがダングリングしないよう登録を解除する
	if (engine_) {
		// エディタ設定セクションの解除（解除時に最終保存が走る。パネル・facade はまだ生存中）
		if (auto* editorSettings = engine_->GetSubsystem<EditorSettingsSubsystem>()) {
			editorSettings->UnregisterSections(this);
		}
		if (auto* debug = engine_->GetDebugSubsystem()) {
			if (auto* ui = debug->GetGameDebugUI()) {
				ui->UnregisterEnvironmentEditor(kEditorLabel, this);
			}
		}
		// 解除済みなので、保険で呼ばれるデストラクタ側では何もしない（最終保存の二重実行を防ぐ）
		engine_ = nullptr;
	}
	// 設定セクションが保持している Feature 参照も切る
	settingsSection_.Initialize(&parameterPanel_, nullptr, &editorFacade_);
#endif
}

void WaterSceneController::Initialize(
	[[maybe_unused]] WaterRenderFeature* waterFeature,
	[[maybe_unused]] EngineSystem& engine) {
	waterFeature_ = waterFeature;
	if (!waterFeature_) {
		return;
	}

#ifdef USE_IMGUI
	engine_ = &engine;
	// UI は facade 経由で Water 設定を取得・適用する
	editorFacade_.Initialize(*waterFeature_, engine);
	// 通常パラメータ編集とデバッグ表示の各パネルを初期化する
	parameterPanel_.Initialize(*waterFeature_, editorFacade_);
	debugPanel_.Initialize(*waterFeature_);

	// Hierarchy の Environment ツリーへ登録し、選択時に Inspector で編集できるようにする
	if (auto* debug = engine.GetDebugSubsystem()) {
		if (auto* ui = debug->GetGameDebugUI()) {
			ui->RegisterEnvironmentEditor(kEditorLabel, this, [this]() { DrawImGuiContent(); });
		}
	}

	// エディタ設定の自動保存（登録時に前回状態が復元される）。
	// parameterPanel_.Initialize の既定プリセット適用より後に登録することで復元値が起点になる
	settingsSection_.Initialize(&parameterPanel_, waterFeature_, &editorFacade_);
	if (auto* editorSettings = engine.GetSubsystem<EditorSettingsSubsystem>()) {
		editorSettings->RegisterSection(&settingsSection_, this);
	}
#endif
}

#ifdef USE_IMGUI
void WaterSceneController::DrawImGuiContent() {
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
