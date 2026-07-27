#include "pch.h"
#include "WaterSceneController.h"

#include "WaterTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/Logger/Logger.h"

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
	}
#endif
}

void WaterSceneController::Initialize(WaterTestScene& scene, EngineSystem& engine) {
	// scene setup でオブジェクトを構築し、その結果を runtime 制御へ渡す
	const WaterSceneObjects sceneObjects = sceneSetup_.SetupScene(scene, engine);
	runtimeController_.Initialize(sceneObjects);

#ifdef USE_IMGUI
	engine_ = &engine;
	// UI は facade 経由で Water 設定を取得・適用する
	editorFacade_.Initialize(runtimeController_, engine);
	// 通常パラメータ編集とデバッグ表示の各パネルを初期化する
	parameterPanel_.Initialize(runtimeController_, editorFacade_);
	debugPanel_.Initialize(runtimeController_);

	// Hierarchy の Environment ツリーへ登録し、選択時に Inspector で編集できるようにする
	if (auto* debug = engine.GetDebugSubsystem()) {
		if (auto* ui = debug->GetGameDebugUI()) {
			ui->RegisterEnvironmentEditor(kEditorLabel, this, [this]() { DrawImGuiContent(); });
		}
	}

	// エディタ設定の自動保存（登録時に前回状態が復元される）。
	// parameterPanel_.Initialize の既定プリセット適用より後に登録することで復元値が起点になる
	settingsSection_.Initialize(&parameterPanel_, &runtimeController_, &editorFacade_);
	if (auto* editorSettings = engine.GetSubsystem<EditorSettingsSubsystem>()) {
		editorSettings->RegisterSection(&settingsSection_, this);
	}
#endif

	// 初期状態の波面データを DXR 側参照用に構築する
	runtimeController_.UpdateWaterRefractionSurfaceData();
}

void WaterSceneController::Update(EngineSystem& engine, float deltaTime, bool atmosphereSky) {
	// 水面アニメーションと時間進行を更新する
	runtimeController_.UpdateSimulation(deltaTime);

	// 大気散乱の空気遠近感を水面へ適用するか（SyncFrameResources が参照する）
	runtimeController_.SetAtmosphereSkyEnabled(atmosphereSky);

	// 水面制御用 UI は Hierarchy の Environment ツリーに登録済み（選択時に Inspector で編集）

	// UI やアニメーション結果を反映した最新の水面データを再構築する
	runtimeController_.UpdateWaterRefractionSurfaceData();

	if (WaterPlaneObject* waterPlane = runtimeController_.GetWaterPlane()) {
		const WaterFrameConstants& frameConstants = waterPlane->GetFrameConstants();
		const WaterSurfaceData* surfaceData = runtimeController_.GetWaterRefractionSurfaceData();
		// デバッグ可視化中は水面サーフェスデータの更新内容をログへ出力する
		if (surfaceData && frameConstants.depthFadeDebugEnabled != 0) {
			Logger::GetInstance().Infof(
				LogCategory::Graphics,
				LogSubCategory::Pipeline,
				"WaterSceneController: 水面データ更新 height={:.3f} activeWaveCount={} time={:.3f} debugMode={}",
				surfaceData->waterHeight,
				surfaceData->activeWaveCount,
				surfaceData->time,
				frameConstants.depthDebugViewMode);
		}

		// 水面描画に必要な各種 SRV とフレーム定数を更新する
		runtimeController_.SyncFrameResources(engine);
		waterPlane->UpdateFrameConstants();
	}
}

float WaterSceneController::GetWaterHeight() const {
	return runtimeController_.GetWaterHeight();
}

const WaterSurfaceData* WaterSceneController::GetWaterRefractionSurfaceData() const {
	return runtimeController_.GetWaterRefractionSurfaceData();
}

#ifdef USE_IMGUI
void WaterSceneController::DrawImGuiContent() {
	if (!runtimeController_.GetWaterPlane()) {
		ImGui::TextDisabled("水面オブジェクトがありません");
		return;
	}

	// 見た目調整とデバッグ機能を用途別に分けて表示する
	ImGui::TextDisabled("見た目調整とデバッグ表示を分けて管理します。");
	ImGui::Spacing();

	parameterPanel_.Draw(runtimeController_, editorFacade_);
	debugPanel_.Draw(runtimeController_, editorFacade_);
}
#endif
