#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "Editor/Water/WaterEditorFacade.h"

namespace CoreEngine { class WaterRenderFeature; }

#ifdef CORE_EDITOR

/// @brief 水面のインスペクタのうち、保存する値（波・水の色・反射・泡・コースティクス）を描く
/// @details 値は CVar（r.Water.*）が持ち、行の右クリック・Undo・既定値の表示は CVarUI の行が受け持つ。
///          Gerstner の波（FFT を使わないときの波）は保存しない作業用の状態で、水面コンポーネントを直接書き換える。
class WaterSurfaceParameterPanel {
public:
	/// @brief Gerstner の波をプリセットから作る（CVar には触れない）
	void Initialize(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief 各セクションを描く
	void Draw(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

private:
	/// @brief 波（FFT の海の海況とうねり、または Gerstner の波）
	void DrawWaveSection(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief 海況のプリセットを選ぶ行
	void DrawSeaStateRow();

	/// @brief 今の値で出来上がる風波とうねりの大きさの行
	void DrawWaveSummaryRows(WaterEditorFacade& editorFacade);

	/// @brief Gerstner の波のプリセット・本数・個別の波
	void DrawGerstnerRows(CoreEngine::WaterRenderFeature& runtimeController);

	/// @brief 水の色（水質・吸収・散乱・濁り）
	void DrawWaterColorSection();

	/// @brief 反射と屈折
	void DrawSurfaceSection();

	/// @brief 泡
	void DrawFoamSection(CoreEngine::WaterRenderFeature& runtimeController);

	/// @brief コースティクス
	void DrawCausticsSection();

	/// @brief Gerstner の波をプリセットから作り直す
	void RegenerateGerstnerWaves(CoreEngine::WaterRenderFeature& runtimeController);

	/// @brief Gerstner の波のプリセット（WaterPresetType。-1 は水面の既定の波）
	int gerstnerPreset_ = -1;

	/// @brief Gerstner の波の本数
	int gerstnerWaveCount_ = static_cast<int>(kMaxWaterWaveCount);
};

#endif
