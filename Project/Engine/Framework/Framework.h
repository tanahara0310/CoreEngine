#pragma once
#include <memory>

#include "WinApp/WinApp.h"
#include "Utility/Debug/LeakChecker.h"
#include "Utility/Debug/CrashDump.h"
#include <EngineSystem.h>

/// @brief フレームワーク基底クラス - エンジン層の汎用機能を提供
/// ゲーム固有のクラスはこのクラスを継承して実装する

namespace CoreEngine
{
class Framework {
public:
	Framework() = default;
	virtual ~Framework();

	/// @brief フレームワークの実行（ゲームループ）
	void Run();

protected:
	// ──────────────────────────────────────────────────────────
	// 仮想関数 - 派生クラスでオーバーライドする
	// ──────────────────────────────────────────────────────────

	/// @brief 初期化処理（ゲーム固有のリソース読み込み等）
	virtual void Initialize() = 0;

	/// @brief 終了処理（ゲーム固有のリソース解放等）
	virtual void Finalize() = 0;

	/// @brief 更新処理（ゲーム固有のロジック）
	virtual void Update() = 0;

	/// @brief 描画処理（ゲーム固有の描画）
	virtual void Draw() = 0;

	// ──────────────────────────────────────────────────────────
	// エンジンシステムへのアクセス
	// ──────────────────────────────────────────────────────────

	/// @brief エンジンシステムの取得
	/// @return エンジンシステムへのポインタ
	EngineSystem* GetEngineSystem() { return engineSystem_.get(); }

private:
	// ──────────────────────────────────────────────────────────
	// エンジン層の汎用データ（どのゲームでも使う）
	// ──────────────────────────────────────────────────────────

	/// @brief エンジンシステム（DirectX, 入力, オーディオ等の管理）
	std::unique_ptr<EngineSystem> engineSystem_;

	/// @brief ウィンドウアプリケーション
	std::unique_ptr<WinApp> winApp_;

	/// @brief メモリリークチェッカー（デバッグビルドのみ）
	std::unique_ptr<LeakChecker> leakChecker_;

	/// @brief ゲームループ終了フラグ
	bool isEndRequest_ = false;
};
}
