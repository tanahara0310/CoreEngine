#include "pch.h"
#include "Framework.h"
#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Startup/StartupSequence.h"
#include "Startup/StartupProgress.h"
#include "Startup/SplashScreen.h"
#include "Graphics/Shader/Cache/ShaderCacheStore.h"


namespace CoreEngine
{
    Framework::~Framework() = default;

    void Framework::BuildStartupTasks(StartupSequence& sequence)
    {
        sequence.Add("ゲーム初期化", [this] { Initialize(); });
    }

    void Framework::RunStartupSequence(StartupSequence& sequence, const EngineConfig& config)
    {
        // メインウィンドウはまだ非表示。この小さなウィンドウだけがメッセージを処理する
        SplashScreen splash;
        splash.Show(winApp_->GetInstance(), config.GetWindowTitleWide());

        // 1 ステップが長い処理（シェーダ 119 本のコンパイルなど）の内側からも
        // 再描画とメッセージ処理が走るようにする。これが無いと、ステップ 1 つで
        // 5 秒を超えた時点でローディング画面まで「応答なし」になる。
        //
        // sink はローカルの splash を参照で掴むので、初期化中に例外が飛んでも
        // splash より先に必ず外す（外し忘れるとダングリング参照が残る）
        struct SinkGuard {
            ~SinkGuard() { StartupProgress::ClearSink(); }
        } sinkGuard;

        StartupProgress::SetSink([&splash](const char* detail) {
            splash.SetDetail(detail ? detail : "");
            splash.Pump();
            });

        while (sequence.HasNext()) {
            // 「これから実行するステップ」を先に表示してから走らせる
            splash.SetStatus(sequence.GetProgress(), sequence.GetNextLabel());
            splash.Pump(true);

            // 非表示のメインウィンドウにもメッセージを溜めない
            winApp_->ProcessMessage();

            sequence.Step();
        }

        splash.SetStatus(1.0f, "起動完了");
        splash.Pump(true);

        // 各ステップの CPU 時間を残す。起動時間の回帰はこのログの差分で追う
        sequence.LogSummary();

        // シェーダキャッシュのヒット率。期待どおり無効化されたかはここで見る
        ShaderCacheStore::GetInstance().LogSummary();

        splash.Close();
    }

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

        // ウィンドウアプリケーションの生成。
        // この時点ではウィンドウを表示しない（WinApp::ShowMainWindow のコメント参照）
        winApp_ = std::make_unique<WinApp>();
        winApp_->Initialize(config.windowWidth, config.windowHeight, config.GetWindowTitleWide().c_str());

        // エンジンとゲームの初期化を「1 ステップずつ進められる列」に組み立ててから回す。
        // 一息に実行するとその間メッセージポンプが回らず「応答なし」になるため
        engineSystem_ = std::make_unique<EngineSystem>();

        StartupSequence sequence;
        engineSystem_->BuildStartupTasks(sequence, winApp_.get(), config,
            // ゲーム固有のアセット先読み。エンジン側の都合の良い位置
            //（ModelManager 生成直後・シェーダコンパイル前）へ差し込まれる
            [this](StartupSequence& s) { BuildPreloadTasks(s); });
        BuildStartupTasks(sequence);   // ゲーム固有の初期化（派生クラスで実装）

        RunStartupSequence(sequence, config);

        // 最初のフレームを描ける状態になったのでメインウィンドウを表示する
        winApp_->ShowMainWindow();

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
