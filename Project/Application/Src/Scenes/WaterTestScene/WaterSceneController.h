#pragma once

#ifdef USE_IMGUI
#include "WaterEditorFacade.h"
#include "WaterSurfaceDebugPanel.h"
#include "WaterSurfaceParameterPanel.h"
#include "WaterSettingsSection.h"
#endif

namespace CoreEngine {
	class EngineSystem;
	class WaterRenderFeature;
}

/// @brief 水面エディタ UI の組み立てと Hierarchy への登録を担当する
/// @details 水面本体・波シミュレーション・毎フレームのリソース結線は
///          Engine 側の WaterRenderFeature が持つ。ここは UI だけを扱う。
class WaterSceneController {
public:
	/// @brief 環境エディタの登録を解除する
	~WaterSceneController();

	/// @brief 水面 UI の各パネルを初期化し、環境エディタとして登録する
	/// @param waterFeature シーンへ登録済みの水面 Feature（nullptr のとき UI は出ない）
	/// @param engine エンジンシステム
	void Initialize(CoreEngine::WaterRenderFeature* waterFeature, CoreEngine::EngineSystem& engine);

	/// @brief UI 登録を解除し、Feature 参照を切る
	/// @details Feature の所有者は BaseScene（Finalize で features_ が破棄される）なので、
	///          それより先に呼ぶこと。WaterTestScene::OnFinalize() から呼ばれる。
	///          冪等。デストラクタからも保険として呼ぶ。
	void Shutdown();

private:
#ifdef USE_IMGUI
	/// @brief 水面制御用 UI の内容を描画する（Inspector 内に埋め込み）
	void DrawImGuiContent();
	/// @brief 水面の通常パラメータ編集パネル
	WaterSurfaceParameterPanel parameterPanel_{};
	/// @brief 水面のデバッグ表示・診断パネル
	WaterSurfaceDebugPanel debugPanel_{};
	/// @brief Water UI と Engine 内部設定の仲介 facade
	WaterEditorFacade editorFacade_{};
	/// @brief 環境エディタの登録解除に使うエンジン参照（非所有）
	CoreEngine::EngineSystem* engine_ = nullptr;

	/// @brief 水面エディタ設定の自動保存セクション（登録は Initialize、解除はデストラクタ）
	WaterSettingsSection settingsSection_{};
#endif

	/// @brief UI の操作対象（所有権は BaseScene の Feature 一覧）
	CoreEngine::WaterRenderFeature* waterFeature_ = nullptr;
};
