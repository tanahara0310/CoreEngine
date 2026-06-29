#pragma once

#include "Sample/TestGameObject/Primitive/WaterConstantBuffer.h"

class WaterSurfaceRuntimeController;

namespace CoreEngine {
	class EngineSystem;
}

#ifdef USE_IMGUI
class WaterSurfaceDebugPanel {
public:
	/// @brief デバッグパネルの初期状態を水面へ反映する
	void Initialize(WaterSurfaceRuntimeController& runtimeController);

	/// @brief 水面デバッグ表示と診断 UI を描画する
	void Draw(WaterSurfaceRuntimeController& runtimeController, CoreEngine::EngineSystem& engine);

private:
	/// @brief FFT Ocean の診断 UI を描画する
	void DrawFFTOceanDebugSection(WaterSurfaceRuntimeController& runtimeController, CoreEngine::EngineSystem& engine);

	/// @brief コースティクスのデバッグ UI を描画する
	void DrawCausticsDebugSection(CoreEngine::EngineSystem& engine);

	/// @brief Depth Fade デバッグ表示の有効フラグ
	bool depthFadeDebugEnabled_ = false;
	/// @brief Depth Fade デバッグ表示倍率
	float depthFadeDebugScale_ = 1.5f;
	/// @brief 水面デバッグ可視化モード
	int depthDebugViewMode_ = static_cast<int>(WaterDebugViewMode::RawDepth);
};
#endif
