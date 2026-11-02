#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "Editor/Water/WaterEditorFacade.h"
#include "Utility/JsonManager/JsonManager.h"

#include <initializer_list>
#include <string>

namespace CoreEngine
{
	class WaterSurfaceComponent;
	class WaterSystem;

	namespace Reflection { struct TypeDescriptor; }
	namespace InspectorRenderer { struct DrawContext; }
}

#ifdef CORE_EDITOR

/// @brief 水面のインスペクタの欄（水面 1 枚ごとの値・水の性質・シーンの風・プロジェクトの品質の設定）を描く
/// @details 水の性質は水面コンポーネントの記述子の欄で、Undo とプレハブの印はインスペクタの欄と同じに付く。
///          風はシーンの CVar（r.Wind.*）、描画の品質と負荷はプロジェクトの CVar（r.Water.*）の行で描く。
///          Gerstner の波（FFT を使わないときの波）は保存しない作業用の状態で、水面コンポーネントを直接書き換える。
class WaterSurfaceParameterPanel {
public:
	/// @brief Gerstner の波の本数を水面から読む
	void Initialize(CoreEngine::WaterSystem& runtimeController, WaterEditorFacade& editorFacade);

	/// @brief 各セクションを描く
	/// @param runtimeController 水面一式の Feature（波の大きさ・泡の割合の表示に使う。無ければ nullptr）
	/// @return 水面の値を変えたら true
	bool Draw(CoreEngine::WaterSurfaceComponent& water, const CoreEngine::InspectorRenderer::DrawContext& context,
		CoreEngine::WaterSystem* runtimeController, WaterEditorFacade& editorFacade);

private:
	/// @brief 描く相手（水面コンポーネントと、その記述子・インスペクタの文脈）
	struct Target {
		CoreEngine::WaterSurfaceComponent& water;
		const CoreEngine::Reflection::TypeDescriptor& type;
		void* instance;
		const CoreEngine::InspectorRenderer::DrawContext& context;

		/// @brief 名前で選んだ欄を並べた順に描く
		bool Rows(std::initializer_list<const char*> names) const;

		/// @brief 保存形の値をまとめて入れ、1 件の履歴にする
		bool Assign(const json& values, const std::string& label) const;
	};

	/// @brief プロファイル（指す先と、書き込む・読み直す・新しく作るボタン）
	bool DrawProfileSection(const Target& target);

	/// @brief 波（シーンの風、FFT の海の風波とうねり、または Gerstner の波）
	bool DrawWaveSection(const Target& target, WaterEditorFacade& editorFacade);

	/// @brief 海況のプリセットを選ぶ行
	bool DrawSeaStateRow(const Target& target);

	/// @brief 今の値で出来上がる風波とうねりの大きさの行
	void DrawWaveSummaryRows(WaterEditorFacade& editorFacade);

	/// @brief Gerstner の波のプリセット・本数・個別の波
	void DrawGerstnerRows(CoreEngine::WaterSurfaceComponent& water);

	/// @brief 水の色（水質・吸収・散乱・濁り）
	bool DrawWaterColorSection(const Target& target);

	/// @brief 反射と屈折
	bool DrawSurfaceSection(const Target& target);

	/// @brief 泡
	bool DrawFoamSection(const Target& target, CoreEngine::WaterSystem* runtimeController);

	/// @brief 濡れ跡（プロジェクトの設定）
	void DrawWetnessSection();

	/// @brief コースティクス（プロジェクトの設定）
	void DrawCausticsSection(const Target& target);

	/// @brief Gerstner の波をプリセットから作り直す
	void RegenerateGerstnerWaves(CoreEngine::WaterSurfaceComponent& water);

	/// @brief Gerstner の波のプリセット（WaterPresetType。-1 は水面の既定の波）
	int gerstnerPreset_ = -1;

	/// @brief Gerstner の波の本数
	int gerstnerWaveCount_ = static_cast<int>(kMaxWaterWaveCount);
};

#endif
