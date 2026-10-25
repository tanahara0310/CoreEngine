#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "Editor/Water/WaterEditorFacade.h"

namespace CoreEngine { class WaterRenderFeature; }

#ifdef CORE_EDITOR

/// @brief 水面のインスペクタのうち、デバッグ表示と診断（保存しない）を描く
class WaterSurfaceDebugPanel {
public:
	/// @brief デバッグ表示の初期状態を水面へ入れる
	void Initialize(CoreEngine::WaterRenderFeature& runtimeController);

	/// @brief 「デバッグ」セクションを描く
	void Draw(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

private:
	/// @brief 今の状態（描き方・FFT の準備・水面の高さ・反射）
	void DrawStatusRows(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief 水面の可視化
	void DrawSurfaceView(CoreEngine::WaterRenderFeature& runtimeController);

	/// @brief レイトレの屈折の可視化
	void DrawRefractionDebug(WaterEditorFacade& editorFacade);

	/// @brief コースティクスの可視化
	void DrawCausticsDebug(WaterEditorFacade& editorFacade);

	/// @brief 水面の可視化を有効にするか
	bool surfaceViewEnabled_ = false;

	/// @brief 水面の可視化の倍率
	float surfaceViewScale_ = 1.5f;

	/// @brief 水面の可視化のモード（WaterDebugViewMode）
	int surfaceViewMode_ = static_cast<int>(WaterDebugViewMode::RawDepth);
};

#endif
