#pragma once

#include "WaterSceneSetup.h"
#include "WaterSurfaceRuntimeController.h"

class WaterTestScene;

#ifdef USE_IMGUI
#include "WaterEditorFacade.h"
#include "WaterSurfaceDebugPanel.h"
#include "WaterSurfaceParameterPanel.h"
#endif

class WaterSceneController {
public:
	/// @brief 水面制御の各補助クラスを初期化する
	/// @param scene 水面オブジェクト生成元のシーン
	/// @param engine エンジンシステム
	void Initialize(WaterTestScene& scene, CoreEngine::EngineSystem& engine);

	/// @brief 水面制御全体のフレーム更新を行う
	/// @param engine エンジンシステム
	/// @param deltaTime 前フレームからの経過時間（秒）
	void Update(CoreEngine::EngineSystem& engine, float deltaTime);

	/// @brief ReflectionView の出力を水面描画へ適用する
	/// @param result 反射描画結果
	void ApplyWaterRenderViewResult(const CoreEngine::RenderViewResult& result);

	/// @brief 現在操作中の水面オブジェクトを返す
	WaterPlaneObject* GetWaterPlane() const { return runtimeController_.GetWaterPlane(); }

	/// @brief 現在の水面高さを返す
	float GetWaterHeight() const;

	/// @brief DXR 水面屈折用の波面データを返す
	const CoreEngine::WaterSurfaceData* GetWaterRefractionSurfaceData() const;

private:
#ifdef USE_IMGUI
	/// @brief 水面制御用 ImGui ウィンドウを描画する
	void DrawImGui();
	/// @brief 水面の通常パラメータ編集パネル
	WaterSurfaceParameterPanel parameterPanel_{};
	/// @brief 水面のデバッグ表示・診断パネル
	WaterSurfaceDebugPanel debugPanel_{};
	/// @brief Water UI と Engine 内部設定の仲介 facade
	WaterEditorFacade editorFacade_{};
#endif

	/// @brief 水面オブジェクト生成とフレーム同期を担当するランタイム制御
	WaterSurfaceRuntimeController runtimeController_{};
	/// @brief WaterTestScene の初期オブジェクト生成と初期状態設定
	WaterSceneSetup sceneSetup_{};
};
