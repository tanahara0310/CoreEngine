#include "DebugSubsystem.h"

#ifdef USE_IMGUI

#include "../EngineSystem.h"
#include "EngineProfileScope.h"
#include "../EngineConfig.h"

#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"
#include "Threading/ThreadPool.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/Pass/GBufferPass.h"
#include "Graphics/Render/Pass/DeferredLightingPass.h"
#include "Graphics/Render/Pass/GeometryPass.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Input/InputManager.h"
#include "Scene/SceneManager.h"
#include "ObjectCommon/GameObjectManager.h"

namespace CoreEngine
{
    DebugSubsystem::DebugSubsystem()
        : imGui_(std::make_unique<ImGuiManager>())
        , gameDebugUI_(std::make_unique<GameDebugUI>())
    {
    }

    DebugSubsystem::~DebugSubsystem() = default;

    void DebugSubsystem::Initialize(EngineSystem* engine, const EngineConfig& config)
    {
        engine_ = engine;

        auto* dx = engine_->GetComponent<DirectXCommon>();

        // ImGuiマネージャークラスの初期化
        imGui_->Initialize(engine_->GetWinApp()->GetHwnd(), dx);

        // Canvas プレビュー ビューポートの初期化（背景テクスチャ読み込み）
        if (auto* canvasViewport = imGui_->GetCanvasViewport()) {
            canvasViewport->Initialize(engine_);
        }

        // GPU タイムスタンププロファイラーの初期化
        gpuProfiler_.Initialize(dx->GetDevice());

        // ゲームデバッグUIの初期化（DockingUIを渡す）
        gameDebugUI_->Initialize(engine_, imGui_->GetDockingUI());

        // LoggerからConsoleUIへのログ転送を接続
        if (auto* console = GetConsole()) {
            Logger::GetInstance().SetConsoleCallback(
                [console](LogLevel level, const std::string& category, const std::string& message) {
                    ConsoleLogLevel consoleLevel = ConsoleLogLevel::Info;
                    switch (level) {
                    case LogLevel::Debug:
                    case LogLevel::Trace:
                        consoleLevel = ConsoleLogLevel::Debug;
                        break;
                    case LogLevel::Info:
                        consoleLevel = ConsoleLogLevel::Info;
                        break;
                    case LogLevel::Warn:
                        consoleLevel = ConsoleLogLevel::Warning;
                        break;
                    case LogLevel::Error:
                    case LogLevel::Critical:
                        consoleLevel = ConsoleLogLevel::Error;
                        break;
                    }
                    console->AddLog(message, consoleLevel, category);
                });
        }

        // スレッドプールプロファイラーの初期化
        threadProfilerUI_ = std::make_unique<ThreadProfilerUI>();
        threadProfilerUI_->RegisterPool("TextureLoader",
            []() { return TextureManager::GetInstance().GetThreadPool(); });
        threadProfilerUI_->RegisterPool("ModelLoader", [this]() -> ThreadPool* {
            if (auto* mm = engine_->GetComponent<ModelManager>()) { return mm->GetThreadPool(); }
            return nullptr;
            });
        gameDebugUI_->RegisterEnginePanel("Thread Profiler", [this]() {
            threadProfilerUI_->Draw();
            });

        // キーコンフィグUIの登録
        gameDebugUI_->RegisterEnginePanel("Key Config", [this]() {
            if (auto* inputManager = engine_->GetComponent<InputManager>()) {
                keyConfigUI_.Draw(inputManager->GetQuery());
            }
            });

        // その他の固定ウィンドウをドッキングシステムに登録
        DockingUI* dockingUI = imGui_->GetDockingUI();
        if (dockingUI) {
            // GameViewportが作成するウィンドウを中央に配置
            dockingUI->RegisterWindow("Game", DockArea::Center);

            // SceneViewportが作成するウィンドウを中央に配置
            dockingUI->RegisterWindow("Scene", DockArea::Center);

            // Canvasプレビューウィンドウを Game と同じ位置にタブとして配置
            dockingUI->RegisterWindow("Canvas", DockArea::Center);

            // パーティクルシステムデバッグを右側に配置
            dockingUI->RegisterWindow("Particle System Debug", DockArea::Right);
        }

        // SceneView 設定をコンフィグから読み込み
        sceneViewEnablePostEffect_ = config.sceneViewEnablePostEffect;
    }

    void DebugSubsystem::Finalize()
    {
        // コンソールUIへのログ転送を解除（ImGui解放前に行う）
        Logger::GetInstance().ClearConsoleCallback();

        // プロファイラーの終了処理（ImGui より先に解放）
        gpuProfiler_.Finalize();

        // ImGuiの終了処理
        if (imGui_) {
            imGui_->Finalize();
        }
    }

    void DebugSubsystem::BeginFrame()
    {
        if (!engine_) {
            return;
        }

        auto* sceneManager = engine_->GetSceneManager();

        if (sceneManager) {
            if (auto* sceneViewport = imGui_->GetSceneViewport()) {
                sceneViewport->SetCamera(sceneManager->GetSceneViewCamera());
                sceneViewport->SetGameCamera3D(sceneManager->GetGameViewCamera3D());
                sceneViewport->SetCamera2D(sceneManager->GetGameViewCamera2D());
            }
        }

        // SceneViewportにInputQueryを渡す（ギズモ操作のキーコンフィグ対応）
        if (auto* sceneViewport = imGui_->GetSceneViewport()) {
            if (auto* inputManager = engine_->GetComponent<InputManager>()) {
                sceneViewport->SetInputQuery(&inputManager->GetQuery());
            }
        }

        // ImGuiの開始（PostEffectManagerとGameDebugUIを渡す）
        if (auto* postEffect = engine_->GetComponent<PostEffectManager>()) {
            imGui_->Begin(postEffect, engine_->GetComponent<Render>(), gameDebugUI_.get());
        }

        //メニューバーを最初に描画（ドッキングスペースより前）
        gameDebugUI_->ShowMainMenuBar();

        // その他のデバッグUIの更新（メニューバー以外）
        gameDebugUI_->UpdateDebugPanels();

        if (sceneManager) {
            if (auto* sceneViewport = imGui_->GetSceneViewport()) {
                if (auto* gameObjectManager = sceneManager->GetCurrentGameObjectManager()) {
                    if (auto* sceneCamera = sceneManager->GetSceneViewCamera()) {
                        sceneViewport->UpdateObjectSelection(gameObjectManager, sceneCamera);
                    }
                    if (auto* camera2D = sceneManager->GetGameViewCamera2D()) {
                        sceneViewport->UpdateSpriteSelection(gameObjectManager, camera2D);
                    }
                }
            }
        }
    }

    void DebugSubsystem::EndFrame()
    {
        if (imGui_) {
            imGui_->End();
        }
    }

    void DebugSubsystem::BeginRenderPipeline(ID3D12GraphicsCommandList* cmdList, UINT frameIndex)
    {
        gpuProfiler_.NewFrame(frameIndex);
        gpuProfiler_.BeginCpuTimestamp(GpuTimestampSlot::Total);
        gpuProfiler_.BeginGpuTimestamp(GpuTimestampSlot::Total, cmdList);
    }

    void DebugSubsystem::DrawImGuiWithProfiling(ID3D12GraphicsCommandList* cmdList)
    {
        EngineProfileScope scope(engine_, GpuTimestampSlot::ImGuiDraw, cmdList);
        if (imGui_) {
            // PostEffectPass完了後に最新の finalDisplayHandle_ でGameビューを描画
            auto* dx = engine_->GetComponent<DirectXCommon>();
            auto* postEffect = engine_->GetComponent<PostEffectManager>();
            imGui_->DrawGameViewport(dx, postEffect);
            imGui_->Draw();
        }
    }

    void DebugSubsystem::EndRenderPipeline(ID3D12GraphicsCommandList* cmdList, UINT frameIndex)
    {
        gpuProfiler_.EndCpuTimestamp(GpuTimestampSlot::Total);
        gpuProfiler_.EndGpuTimestamp(GpuTimestampSlot::Total, cmdList);
        gpuProfiler_.ResolveAll(cmdList, frameIndex);
    }

    void DebugSubsystem::PostFinalizeFrame(DirectXCommon* dx)
    {
        if (!dx) {
            return;
        }
        const UINT nextFrameIndex = dx->GetSwapChain()->GetCurrentBackBufferIndex();
        gpuProfiler_.ReadResults(dx->GetCommandQueue(), nextFrameIndex);

        if (auto* dockingUI = GetDockingUI()) {
            dockingUI->SetTimingData(gpuProfiler_.GetResults());
        }
    }

    void DebugSubsystem::RenderSceneView(
        RenderContext& context,
        RenderPipeline* renderPipeline,
        Render* render,
        ID3D12GraphicsCommandList* cmdList,
        PassOutput& inOutPreviousOutput,
        const std::function<void(RenderPass*)>& executePass,
        const std::function<void()>& gameRenderCallback)
    {
        if (!imGui_ || !renderPipeline || !render) {
            return;
        }

        if (!imGui_->IsSceneViewVisible()) {
            return;
        }

        auto* sceneManager = engine_->GetSceneManager();
        if (!sceneManager) {
            return;
        }

        auto* sceneViewTarget = render->GetRenderTarget("SceneView");
        if (!sceneViewTarget) {
            return;
        }

        EngineProfileScope scope(engine_, GpuTimestampSlot::SceneView, cmdList);

        // ゲームパスのために previousOutput を保存（SceneView パス後に復元）
        PassOutput savedOutput = inOutPreviousOutput;

        // SceneView カメラ・レンダリング状態をセットアップ
        sceneManager->SetupSceneViewCamera();

        // 1. GBufferPass: 不透明オブジェクトを G-Buffer に書き込む（Scene カメラ使用）
        executePass(renderPipeline->GetPass<GBufferPass>());

        // SceneView は RTシャドウを使わない（重いためエディタビューでは省略）
        // 代わりに ShadowMap ベースのシャドウのみ使用
        context.currentRTShadowViewId = static_cast<uint32_t>(RayTracingShadowManager::ViewID::SceneView);

        // 2. DeferredLightingPass: G-Buffer を読み取り PBR+IBL ライティングを計算
        executePass(renderPipeline->GetPass<DeferredLightingPass>());

        // 3. GeometryPass: スカイボックス・グリッド・透過オブジェクトを重ねて描画
        if (auto* geometryPass = renderPipeline->GetPass<GeometryPass>()) {
            geometryPass->SetRenderCallback([sceneManager]() { sceneManager->DrawSceneViewGeometry(); });
        }
        executePass(renderPipeline->GetPass<GeometryPass>());
        // ゲームビュー用コールバックに戻す
        if (auto* geometryPass = renderPipeline->GetPass<GeometryPass>()) {
            geometryPass->SetRenderCallback(gameRenderCallback);
        }

        // 4. SceneView RT にコピー
        // sceneViewEnablePostEffect_=true: ポストエフェクトチェーン適用（高品質・高負荷）
        // sceneViewEnablePostEffect_=false: FullScreen blit のみ（軽量）
        if (auto* postEffect = engine_->GetComponent<PostEffectManager>()) {
            D3D12_GPU_DESCRIPTOR_HANDLE srcHandle = sceneViewEnablePostEffect_
                ? postEffect->ExecuteEffectChain(inOutPreviousOutput.srvHandle)
                : inOutPreviousOutput.srvHandle;
            sceneViewTarget->Begin(cmdList);
            postEffect->ExecuteEffect("FullScreen", srcHandle);
            sceneViewTarget->End(cmdList);
        }

        // SceneView カメラ・レンダリング状態を復元
        sceneManager->RestoreGameViewCamera();

        // ゲームパス用に previousOutput を復元
        inOutPreviousOutput = savedOutput;
    }
}

#endif // USE_IMGUI
