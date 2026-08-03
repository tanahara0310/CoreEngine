#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "WaterEditorFacade.h"

namespace CoreEngine { class WaterRenderFeature; }
namespace CoreEngine { class WaterPlaneObject; }

#ifdef USE_IMGUI
/// @brief 水面パラメータ編集パネル
/// @details Phase 5 で全パラメータを WaterCVars（単一情報源）へ移行した。
///          このパネルは CVar のストレージを直接編集する薄い UI で、
///          値の適用は WaterRenderFeature::ApplySettingsFromCVars（毎フレーム）が担い、
///          永続化は CVars.json（CVarSettingsSection）に一本化されている。
///          旧 Water.json / UI キャッシュ構造体 / 専用シリアライズは廃止済み。
///          Gerstner の波生成ツール・個別波編集はワークフロー状態のため
///          CVar 化せず、従来どおり WaterPlaneObject を直接操作する（非永続）。
class WaterSurfaceParameterPanel {
public:
	/// @brief パラメータパネルの初期状態を構築する
	/// @details CVar は登録時（CVarSettingsSection）に復元済みのため値には触れない。
	///          非永続の Gerstner 波だけをプリセットから生成する
	void Initialize(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief 水面パラメータ編集 UI を描画する
	void Draw(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

private:
	/// @brief 指定プリセットの値を WaterCVars へ適用し、Gerstner 波を再生成する
	void ApplyWaterPreset(CoreEngine::WaterRenderFeature& runtimeController, WaterPresetType preset);
	/// @brief 指定プリセットの推奨波数を水面へ反映する
	void RestoreRecommendedWaveCount(CoreEngine::WaterRenderFeature& runtimeController, WaterPresetType preset);
	/// @brief プリセット設定に基づくレイヤー波を再生成する
	void RegenerateLayeredWaves(CoreEngine::WaterRenderFeature& runtimeController, WaterPresetType preset, uint32_t activeWaveCount);
	/// @brief 描画方式の切り替えと現在状態を表示する
	void DrawWaterTypeSection(CoreEngine::WaterRenderFeature& runtimeController);
	/// @brief 見た目・反射・透過・泡・UV に関する共通パラメータ UI を描画する
	void DrawCommonParameterSection(CoreEngine::WaterRenderFeature& runtimeController);
	/// @brief コースティクスの見た目パラメータ UI を描画する（デバッグ表示はデバッグパネル側）
	void DrawCausticsSection(WaterEditorFacade& editorFacade);
	/// @brief FFT Ocean に関する描画経路切替とパラメータ UI を描画する
	void DrawFFTOceanSection(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);
	/// @brief FFT Ocean プリセットを WaterCVars へ適用する
	void ApplyFFTOceanPreset(int presetIndex);
	/// @brief Gerstner Wave のプリセット・波生成に関する UI を描画する
	void DrawGerstnerWaveSection(CoreEngine::WaterRenderFeature& runtimeController);
	/// @brief 個別波編集 UI を描画する
	void DrawIndividualWaveEditor(CoreEngine::WaterRenderFeature& runtimeController);

	/// @brief 波生成補助機能の UI 状態（ワークフロー状態。非永続）
	struct WaveToolState {
		int preset = static_cast<int>(WaterPresetType::Lake);
		int activeWaveCount = static_cast<int>(kMaxWaterWaveCount);
		bool lockRecommendedWaveCount = false;
		bool autoRestoreRecommendedWaveCount = true;
		bool autoGenerateOnWaveCountIncrease = true;
	} waveToolState_{};

	/// @brief FFT 海況プリセットのコンボ表示インデックス（実体は WaterCVars。導出値のため非永続）
	int fftPresetIndex_ = 0;
};
#endif
