#pragma once
#include "Engine/Utility/Debug/ImGui/Gizmo.h"
#include "Engine/Utility/Debug/ConsoleUI.h"
#include "Engine/Utility/Debug/ImGui/SceneManagerTab.h"

/// @brief デバッグ用のUIクラス
/// エンジンシステムの低レベル情報の表示とデバッグ支援を行う

namespace CoreEngine
{

class EngineSystem; // 前方宣言
class DockingUI; // 前方宣言
class FrameRateController; // 前方宣言
class SceneManager; // 前方宣言

class GameDebugUI {
public:
	/// @brief 初期化
	/// @param engine エンジンシステム
	/// @param dockingUI ドッキングUI（ウィンドウ登録用）
	void Initialize(EngineSystem* engine, DockingUI* dockingUI = nullptr);

	/// @brief シーンマネージャーの設定
	/// @param sceneManager SceneManagerへのポインタ
	void SetSceneManager(SceneManager* sceneManager);

	/// @brief 更新
	void Update();

	/// @brief メニューバーのみを表示（ドッキング前に呼び出す）
	void ShowMainMenuBar();

	/// @brief メニューバー以外のデバッグパネルを表示
	void UpdateDebugPanels();

	/// @brief コンソールUIへのアクセッサ
	/// @return コンソールUIのポインタ
	ConsoleUI* GetConsole() { return console_.get(); }

	/// @brief シーンマネージャータブへのアクセッサ
	/// @return シーンマネージャータブのポインタ
	SceneManagerTab* GetSceneManagerTab() { return sceneManagerTab_.get(); }

	/// @brief テクスチャビューアの表示状態を取得
	/// @return テクスチャビューアの表示状態
	bool IsTextureViewerVisible() const { return showTextureViewer_; }

private: // メンバ変数
	EngineSystem* engine_ = nullptr; // エンジンシステムへのポインタ
	DockingUI* dockingUI_ = nullptr; // ドッキングUIへのポインタ

	// ===== コンソール機能 =====
	std::unique_ptr<ConsoleUI> console_ = std::make_unique<ConsoleUI>();

	// ===== シーンマネージャータブ =====
	std::unique_ptr<SceneManagerTab> sceneManagerTab_ = std::make_unique<SceneManagerTab>();

	// ===== ウィンドウ表示フラグ（最小限） =====
	bool showEngineInfo_ = true;
	bool showConsole_ = true; // コンソールウィンドウの表示フラグ
	bool showSceneManager_ = true; // シーンマネージャーウィンドウの表示フラグ
	bool showTextureViewer_ = false; // テクスチャビューアの表示フラグ

	// ウィンドウ名定数（変更しやすくするため）
	static constexpr const char* engineDebugWindow = "Engine Debug Info";
	static constexpr const char* LightWindow = "Lighting";
	static constexpr const char* consoleWindow = "Console"; // コンソールウィンドウ
	static constexpr const char* sceneManagerWindow = "Scene Manager"; // シーンマネージャーウィンドウ

private: // デバッグ用のUIを表示するためのメソッド
	/// @brief エンジン情報UIを表示
	void ShowEngineInfoUI();

	/// @brief FPS情報タブを表示
	void ShowFPSInfoTab(FrameRateController* frameRate);

	/// @brief 詳細パフォーマンスタブを表示
	void ShowDetailedPerformanceTab(FrameRateController* frameRate);

	/// @brief システム状態タブを表示
	void ShowSystemStatusTab();

	/// @brief ライティングデバッグUIを表示（独立ウィンドウ）
	void ShowLightingDebugUI();

	/// @brief コンソールウィンドウを表示
	void ShowConsoleUI();

	/// @brief シーンマネージャーウィンドウを表示
	void ShowSceneManagerUI();

	/// @brief ドッキングシステムにウィンドウを登録
	void RegisterWindowsForDocking();
};
}
