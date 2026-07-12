#include "pch.h"
#include "WaterSceneController.h"

#include "WaterTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/Logger/Logger.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

void WaterSceneController::Initialize(WaterTestScene& scene, EngineSystem& engine) {
	// scene setup でオブジェクトを構築し、その結果を runtime 制御へ渡す
	const WaterSceneObjects sceneObjects = sceneSetup_.SetupScene(scene, engine);
	runtimeController_.Initialize(sceneObjects);

#ifdef USE_IMGUI
	// UI は facade 経由で Water 設定を取得・適用する
	editorFacade_.Initialize(runtimeController_, engine);
	// 通常パラメータ編集とデバッグ表示の各パネルを初期化する
	parameterPanel_.Initialize(runtimeController_, editorFacade_);
	debugPanel_.Initialize(runtimeController_);
#endif

	// 初期状態の波面データを DXR 側参照用に構築する
	runtimeController_.UpdateWaterRefractionSurfaceData();
}

void WaterSceneController::Update(EngineSystem& engine, float deltaTime) {
	// 水面アニメーションと時間進行を更新する
	runtimeController_.UpdateSimulation(deltaTime);

#ifdef USE_IMGUI
	// 水面制御用 UI を描画し、その場で変更を反映する
	DrawImGui();
#endif

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

void WaterSceneController::ApplyWaterRenderViewResult(const RenderViewResult& result) {
	runtimeController_.ApplyWaterRenderViewResult(result);
}

float WaterSceneController::GetWaterHeight() const {
	return runtimeController_.GetWaterHeight();
}

const WaterSurfaceData* WaterSceneController::GetWaterRefractionSurfaceData() const {
	return runtimeController_.GetWaterRefractionSurfaceData();
}

#ifdef USE_IMGUI
void WaterSceneController::DrawImGui() {
	if (!runtimeController_.GetWaterPlane()) {
		return;
	}

	// 見た目調整とデバッグ機能を同一ウィンドウ内で用途別に分けて表示する
	ImGui::SetNextWindowSize(ImVec2(460.0f, 760.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("水面コントローラ")) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("見た目調整とデバッグ表示を分けて管理します。");
	ImGui::Spacing();

	parameterPanel_.Draw(runtimeController_, editorFacade_);
	debugPanel_.Draw(runtimeController_, editorFacade_);

	ImGui::End();
}
#endif
