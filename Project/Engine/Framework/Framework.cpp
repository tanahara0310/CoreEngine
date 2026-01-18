#include "Framework.h"


namespace CoreEngine
{
Framework::~Framework() = default;

void Framework::Run()
{
	// ──────────────────────────────────────────────────────────
	// デバッグ機能の初期化（エンジン層で自動管理）
	// ──────────────────────────────────────────────────────────

	// クラッシュダンプの登録
	CrashDump::Register();

	// メモリリークチェッカーの生成（デバッグビルドのみ）
#ifdef _DEBUG
	leakChecker_ = std::make_unique<LeakChecker>();
#endif

	// ──────────────────────────────────────────────────────────
	// 初期化フェーズ
	// ──────────────────────────────────────────────────────────

	// ウィンドウアプリケーションの生成・初期化
	winApp_ = std::make_unique<WinApp>();
	winApp_->Initialize(WinApp::kClientWidth, WinApp::kClientHeight, L"ガンガガン");

	// エンジンシステムの生成・初期化
	engineSystem_ = std::make_unique<EngineSystem>();
	engineSystem_->Initialize(winApp_.get());

	// ゲーム固有の初期化（派生クラスで実装）
	Initialize();

	// ──────────────────────────────────────────────────────────
	// ゲームループ
	// ──────────────────────────────────────────────────────────

	while (true) {
		// ウィンドウメッセージ処理
		if (winApp_->ProcessMessage()) {
			break; // WM_QUIT メッセージが来たら終了
		}

		// エンジンシステムのフレーム開始処理
		engineSystem_->BeginFrame();

		// ゲーム固有の更新処理（派生クラスで実装）
		Update();

		// ゲーム固有の描画処理（派生クラスで実装）
		engineSystem_->ExecuteRenderPipeline([this]() {
			Draw();
			});

		// エンジンシステムのフレーム終了処理
		engineSystem_->EndFrame();
	}

	// ──────────────────────────────────────────────────────────
	// 終了フェーズ
	// ──────────────────────────────────────────────────────────

	// ゲーム固有の終了処理（派生クラスで実装）
	Finalize();

	// エンジンシステムの終了処理
	engineSystem_->Finalize();

	// ウィンドウアプリケーションの終了処理
	winApp_->CloseAppWindow();
}
}
