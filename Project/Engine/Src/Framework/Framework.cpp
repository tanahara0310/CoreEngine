#include "pch.h"
#include "Framework.h"
#include "Graphics/Render/Pass/RenderPipeline.h"
#ifdef _DEBUG
#include "Utility/Debug/LivePPAgent.h"
#endif


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
        // Live++ エージェントの初期化（デバッグビルドのみ）
        // ※ できる限り早く起動し、エンジン初期化と並行して PDB をロードさせる
        // ──────────────────────────────────────────────────────────
#ifdef _DEBUG
        LivePPAgent livepp;
        livepp.Initialize();
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

#ifdef _DEBUG
            // Live++ ホットリロード / ホットリスタートの処理（フレーム先頭で実行）
            livepp.Update();
#endif

            // エンジンシステムのフレーム開始処理
            engineSystem_->BeginFrame();

            // ゲーム固有の更新処理（派生クラスで実装）
            Update();

            // ゲーム固有の描画処理（派生クラスで実装）
            PrepareRender();
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
