#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "WaterEditorFacade.h"
#include "externals/nlohmann/single_include/nlohmann/json_fwd.hpp"

namespace CoreEngine { class WaterRenderFeature; }
namespace CoreEngine { class WaterPlaneObject; }

#ifdef USE_IMGUI
class WaterSurfaceParameterPanel {
public:
	/// @brief パラメータパネルの初期状態を構築する
	/// @param runtimeController 水面ランタイム制御
	/// @param editorFacade Water UI 用 facade
	void Initialize(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief 水面パラメータ編集 UI を描画する
	/// @param runtimeController 水面ランタイム制御
	/// @param editorFacade Water UI 用 facade
	void Draw(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

	// ===== エディタ設定の自動保存（WaterSettingsSection から委譲） =====

	/// @brief UI キャッシュ（見た目/水質）と FFT・DXR屈折・コースティクス設定を JSON へ書き出す
	/// @note ベース水質（σa/σs）＋濁度は実効係数へ合成後の水面側から復元できないため、
	///       パネルの UI キャッシュが唯一の情報源になる。デバッグ表示系の項目は保存しない
	void SerializeSettings(nlohmann::json& out, const WaterEditorFacade& editorFacade) const;

	/// @brief JSON から UI キャッシュを復元し、水面・facade へ適用する
	/// @note 適用は Draw と同じ経路（WaterPlaneObject の setter / facade の Apply）を使う
	void DeserializeSettings(const nlohmann::json& in,
		CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);

private:
	/// @brief 指定プリセットの値を水面と UI キャッシュへ適用する
	void ApplyWaterPreset(CoreEngine::WaterRenderFeature& runtimeController, WaterPresetType preset);
	/// @brief 指定プリセットの推奨波数を水面へ反映する
	void RestoreRecommendedWaveCount(CoreEngine::WaterRenderFeature& runtimeController, WaterPresetType preset);
	/// @brief プリセット設定に基づくレイヤー波を再生成する
	void RegenerateLayeredWaves(CoreEngine::WaterRenderFeature& runtimeController, WaterPresetType preset, uint32_t activeWaveCount);
	/// @brief 描画方式の切り替えと現在状態を表示する
	void DrawWaterTypeSection(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);
	/// @brief 見た目・反射・透過・UV に関する共通パラメータ UI を描画する
	void DrawCommonParameterSection(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);
	/// @brief FFT Ocean に関する描画経路切替とパラメータ UI を描画する
	void DrawFFTOceanSection(CoreEngine::WaterRenderFeature& runtimeController, WaterEditorFacade& editorFacade);
	/// @brief FFT Ocean プリセットを適用する
	void ApplyFFTOceanPreset(WaterEditorFacade& editorFacade, int presetIndex);
	/// @brief Gerstner Wave のプリセット・波生成に関する UI を描画する
	void DrawGerstnerWaveSection(CoreEngine::WaterRenderFeature& runtimeController);
	/// @brief 個別波編集 UI を描画する
	void DrawIndividualWaveEditor(CoreEngine::WaterRenderFeature& runtimeController);
	/// @brief ベース光学係数と濁度から実効 σa/σs を計算して水面へ反映する
	void ApplyEffectiveOpticalCoefficients(CoreEngine::WaterPlaneObject* waterPlane) const;
	/// @brief 泡パラメータの UI キャッシュを水面へ反映する
	void ApplyFoamParameters(CoreEngine::WaterPlaneObject* waterPlane) const;

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
		// 波長依存の光学特性（ベース値）[1/m]。既定は澄んだ熱帯外洋水（Jerlov I 相当）
		float absorptionCoeff[3] = { 0.35f, 0.07f, 0.02f };
		float scatteringCoeff[3] = { 0.003f, 0.008f, 0.016f };
		// Jerlov 水型プリセットの現在選択（表示用。手動編集後は目安に過ぎない）
		int jerlovPreset = 0;
		// 濁度 0..1。CDOM の青吸収＋懸濁粒子の散乱としてベース値へ加算される
		float turbidity = 0.0f;
	} surfaceParameters_{};

	/// @brief 波生成補助機能の UI 状態
	struct WaveToolState {
		int preset = static_cast<int>(WaterPresetType::Lake);
		int activeWaveCount = static_cast<int>(kMaxWaterWaveCount);
		bool lockRecommendedWaveCount = false;
		bool autoRestoreRecommendedWaveCount = true;
		bool autoGenerateOnWaveCountIncrease = true;
	} waveToolState_{};

	/// @brief FFT Ocean 調整 UI のキャッシュ
	struct FFTOceanParameters {
		int preset = 0;
		bool enabled = true;
		float patchLength = 96.0f;
		float amplitudeScale = 1.0f;
		float windDirection[2] = { 0.92f, 0.38f };
		float windSpeed = 24.0f;
		float choppiness = 1.35f;
		int activeComponentCount = 32;
		float gravity = 9.81f;
		int resolution = 256;
	} fftOceanParameters_{};

	/// @brief 泡（whitecap）調整 UI のキャッシュ（FFTOcean 専用）
	/// @note 既定値は WaterFrameConstants の初期値と一致させること
	struct FoamParameters {
		bool enabled = true;
		// 発生しきい値。合成ヤコビアン detJ がこれを下回ると泡が立つ
		float bias = 0.85f;
		// しきい値からの立ち上がり勾配
		float gain = 4.0f;
		// 泡レイヤの不透明度（白ベタ回避のため 1.0 未満を推奨）
		float opacity = 0.9f;
		// カスケード別の勾配寄与。勾配は波数に比例するため小パッチほど小さくする
		// （無重みだと 31m カスケードが支配して detJ が飽和する: Phase 0 実測）
		float cascadeWeights[3] = { 1.0f, 0.5f, 0.2f };
		// 泡の寿命 τ [s]。砕けた後に白い筋が e^-1 に減衰するまでの時間
		float decaySeconds = 3.0f;
	} foamParameters_{};

	/// @brief DXR 屈折の最大スクリーンずれ量（px）
	float rtRefractionOffsetPixels_ = 8.0f;
};
#endif
