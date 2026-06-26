#pragma once

#include "Sample/TestGameObject/Primitive/WaterConstantBuffer.h"

class WaterSurfaceRuntimeController;

namespace CoreEngine {
	class EngineSystem;
}

#ifdef USE_IMGUI
class WaterSurfaceParameterPanel {
public:
	/// @brief パラメータパネルの初期状態を構築する
	/// @param runtimeController 水面ランタイム制御
	/// @param engine エンジンシステム
	void Initialize(WaterSurfaceRuntimeController& runtimeController, CoreEngine::EngineSystem& engine);

	/// @brief 水面パラメータ編集 UI を描画する
	/// @param runtimeController 水面ランタイム制御
	/// @param engine エンジンシステム
	void Draw(WaterSurfaceRuntimeController& runtimeController, CoreEngine::EngineSystem& engine);

private:
	/// @brief 指定プリセットの値を水面と UI キャッシュへ適用する
	void ApplyWaterPreset(WaterSurfaceRuntimeController& runtimeController, WaterPresetType preset);
	/// @brief 指定プリセットの推奨波数を水面へ反映する
	void RestoreRecommendedWaveCount(WaterSurfaceRuntimeController& runtimeController, WaterPresetType preset);
	/// @brief プリセット設定に基づくレイヤー波を再生成する
	void RegenerateLayeredWaves(WaterSurfaceRuntimeController& runtimeController, WaterPresetType preset, uint32_t activeWaveCount);
	/// @brief DXR 水面屈折設定をエンジン側へ反映する
	void ApplyRayTracingSettings(CoreEngine::EngineSystem& engine) const;
	/// @brief 見た目・反射・透過に関する通常パラメータ UI を描画する
	void DrawRuntimeParameterSection(WaterSurfaceRuntimeController& runtimeController, CoreEngine::EngineSystem& engine);
	/// @brief UV と波生成に関する UI を描画する
	void DrawWaveToolSection(WaterSurfaceRuntimeController& runtimeController);
	/// @brief 個別波編集 UI を描画する
	void DrawIndividualWaveEditor(WaterSurfaceRuntimeController& runtimeController);

	/// @brief 水面の見た目に関する UI キャッシュ
	struct AppearanceParameters {
		float baseColor[4] = { 0.04f, 0.18f, 0.28f, 0.85f };
		float roughness = 0.04f;
		float metallic = 0.0f;
		bool iblEnabled = true;
		float fresnelReflectanceScale = 1.0f;
		float fresnelBaseReflectance = 0.02f;
	} appearanceParameters_{};

	/// @brief 水面の透過・UV・水色に関する UI キャッシュ
	struct SurfaceParameters {
		float scrollSpeed[2] = { 0.03f, 0.01f };
		float uvTiling[2] = { 4.0f, 4.0f };
		bool depthFadeEnabled = true;
		float absorptionCoeff = 1.2f;
		float shallowColor[3] = { 0.10f, 0.85f, 0.65f };
		float deepColor[3] = { 0.02f, 0.08f, 0.45f };
	} surfaceParameters_{};

	/// @brief 波生成補助機能の UI 状態
	struct WaveToolState {
		int preset = static_cast<int>(WaterPresetType::Lake);
		int activeWaveCount = static_cast<int>(kMaxWaterWaveCount);
		bool lockRecommendedWaveCount = false;
		bool autoRestoreRecommendedWaveCount = true;
		bool autoGenerateOnWaveCountIncrease = true;
	} waveToolState_{};

	/// @brief DXR 屈折の最大スクリーンずれ量（px）
	float rtRefractionOffsetPixels_ = 3.0f;
};
#endif
