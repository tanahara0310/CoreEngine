#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "WaterEditorFacade.h"

class WaterSurfaceRuntimeController;

#ifdef USE_IMGUI
class WaterSurfaceDebugPanel {
public:
	/// @brief デバッグパネルの初期状態を水面へ反映する
	void Initialize(WaterSurfaceRuntimeController& runtimeController);

	/// @brief 水面デバッグ表示と診断 UI を描画する
	void Draw(WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade);

private:
	/// @brief 水面共通の診断情報と Depth Fade 可視化 UI を描画する
	void DrawCommonDebugSection(WaterSurfaceRuntimeController& runtimeController);

	/// @brief FFT Ocean の診断 UI を描画する
	void DrawFFTOceanDebugSection(WaterSurfaceRuntimeController& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief Gerstner Wave の診断 UI を描画する
	void DrawGerstnerWaveDebugSection(WaterSurfaceRuntimeController& runtimeController);

	/// @brief コースティクスのデバッグ UI を描画する
	void DrawCausticsDebugSection(WaterEditorFacade& editorFacade);

	/// @brief Depth Fade デバッグ表示の有効フラグ
	bool depthFadeDebugEnabled_ = false;
	/// @brief Depth Fade デバッグ表示倍率
	float depthFadeDebugScale_ = 1.5f;
	/// @brief 水面デバッグ可視化モード
	int depthDebugViewMode_ = static_cast<int>(WaterDebugViewMode::RawDepth);
};
#endif
