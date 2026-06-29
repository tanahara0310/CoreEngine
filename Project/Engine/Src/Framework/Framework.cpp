#include "pch.h"
#include "Framework.h"
#include "Graphics/Render/Pass/RenderPipeline.h"


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
        // エンジンコンフィグの読み込み
        // ──────────────────────────────────────────────────────────
        EngineConfig config = EngineConfig::Load();

        // ──────────────────────────────────────────────────────────
        // 初期化フェーズ
        // ──────────────────────────────────────────────────────────

        // ウィンドウアプリケーションの生成・初期化
        winApp_ = std::make_unique<WinApp>();
        winApp_->Initialize(config.windowWidth, config.windowHeight, config.GetWindowTitleWide().c_str());

        // エンジンシステムの生成・初期化
        engineSystem_ = std::make_unique<EngineSystem>();
        engineSystem_->Initialize(winApp_.get(), config);

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
            PrepareRender();
            engineSystem_->ExecuteRenderPipeline();

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

        // Run 終了前に明示破棄し、終了順を安定させる
        engineSystem_.reset();
        winApp_.reset();

#ifdef _DEBUG
        leakChecker_.reset();
#endif
    }
}
