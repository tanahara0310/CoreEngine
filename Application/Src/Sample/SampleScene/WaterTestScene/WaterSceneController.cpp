#include "pch.h"
#include "WaterSceneController.h"

#include "Application/Src/Sample/SampleScene/WaterTestScene/WaterTestScene.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/Logger/Logger.h"

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

void WaterSceneController::Initialize(WaterTestScene& scene, EngineSystem& engine) {
	// ランタイム制御を初期化し、水面オブジェクト群を生成する
	runtimeController_.Initialize(scene, engine);

#ifdef USE_IMGUI
	// 通常パラメータ編集とデバッグ表示の各パネルを初期化する
	parameterPanel_.Initialize(runtimeController_, engine);
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
	DrawImGui(engine);
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
void WaterSceneController::DrawImGui(EngineSystem& engine) {
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

	parameterPanel_.Draw(runtimeController_, engine);
	debugPanel_.Draw(runtimeController_, engine);

	ImGui::End();
}
#endif
