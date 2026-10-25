#pragma once

#ifdef CORE_EDITOR
#include "Editor/Water/WaterEditorFacade.h"
#include "Editor/Water/WaterSurfaceDebugPanel.h"
#include "Editor/Water/WaterSurfaceParameterPanel.h"
#include "Utility/Lifetime/ScopedRegistration.h"
#endif

namespace CoreEngine {
	class EngineSystem;
	class WaterRenderFeature;
}

namespace CoreEngine {

/// @brief 水面エディタ UI の組み立てとインスペクタへの登録を担当する
/// @details 水面本体・波シミュレーション・毎フレームのリソース結線は
///          Engine 側の WaterRenderFeature が持つ。ここは UI だけを扱う。
class WaterEditorPanel {
public:
	/// @brief 水面 UI の各パネルを初期化し、水面コンポーネントのインスペクタとして登録する
	/// @param waterFeature シーンへ登録済みの水面 Feature（nullptr のとき UI は出ない）
	/// @param engine エンジンシステム
	void Initialize(CoreEngine::WaterRenderFeature* waterFeature, CoreEngine::EngineSystem& engine);

	/// @brief インスペクタの登録を外し、Feature 参照を切る
	/// @note 水面の Feature の Finalize から呼ぶ。何度呼んでもよい。
	void Shutdown();

private:
#ifdef CORE_EDITOR
	/// @brief 水面コンポーネントのインスペクタの、記述子で描けない欄を描く
	void DrawImGuiContent();
	/// @brief 保存する値（波・水の色・反射・泡・コースティクス）
	WaterSurfaceParameterPanel parameterPanel_{};
	/// @brief デバッグ表示と診断（保存しない）
	WaterSurfaceDebugPanel debugPanel_{};
	/// @brief デバッグ表示と診断の仲介
	WaterEditorFacade editorFacade_{};
	/// @brief インスペクタの出し方の登録（破棄すると外れる）
	ScopedRegistration inspector_;
#endif

	/// @brief UI の操作対象（所有権は Scene の Feature 一覧）
	CoreEngine::WaterRenderFeature* waterFeature_ = nullptr;
};

}
