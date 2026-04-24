#include "EngineSystem.h"
#include <cstring>


// ユーティリティ
#include "Utility/Random/RandomGenerator.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Threading/ThreadPool.h"

#if defined(USE_IMGUI) && defined(USE_PIX)
#include "Utility/Debug/ImGui/PixCapture.h"
#endif

// レンダリング関連
#include "Graphics/Render/Render.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/Render/Model/ModelRenderer.h"
#include "Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Shadow/ShadowMapRenderer.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/Render/Particle/ParticleRenderer.h"
#include "Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Graphics/Model/ModelManager.h"

// レンダーパス
#include "Graphics/Render/Pass/ShadowMapPass.h"
#include "Graphics/Render/Pass/GBufferPass.h"
#include "Graphics/Render/Pass/DeferredLightingPass.h"
#include "Graphics/Render/Pass/GeometryPass.h"
#include "Graphics/Render/Pass/PostEffectPass.h"
#include "Graphics/Render/Pass/BackBufferPass.h"

// レイトレーシング
#include "Graphics/RayTracing/RayTracingShadowManager.h"

// 入力管理
#include "Input/InputManager.h"

// フレームレート制御
#include "Utility/FrameRate/FrameRateController.h"

#include "ObjectCommon/GameObject.h"
#include "ObjectCommon/Model/ModelGameObject.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Scene/SceneManager.h"
#include "EngineSystem/EngineConfig.h"


namespace CoreEngine
{
    void EngineSystem::Initialize(WinApp* winApp, const EngineConfig& config)
    {

        // COMの初期化
        CoInitializeEx(0, COINIT_MULTITHREADED);

        // ログシステムの初期化（最初に実行）
        Logger::GetInstance().Initialize();

        // WinAppのインスタンスを保持
        winApp_ = winApp;

        // アセットデータベースの初期化（テクスチャ読み込みより先に必要）
        AssetDatabase::GetInstance().Initialize(std::filesystem::current_path());

        // ===== コンポーネントの作成と初期化 =====

        // フレームレート制御（最初に初期化）
        CreateFrameRateController();

#if defined(USE_IMGUI) && defined(USE_PIX)
        // PIX GPU キャプチャ DLL をロード（D3D12 デバイス作成より前に必要）
        // DLL がロードされると全 D3D12 API がフックされ ~33% のオーバーヘッドが発生するため、
        // コンフィグで明示的に有効化された場合のみロードする
        if (config.enablePixRuntime) {
            PixCapture::LoadPixRuntime();
        }
#endif

        // グラフィックス関連
        CreateGraphicsComponents(config);

        // 入力関連
        CreateInputComponents();

        // オーディオ関連
        CreateAudioComponents();

        // ライト関連（GraphicsComponents 後に初期化）
        CreateLightComponents();

        // 統一乱数生成器の初期化
        RandomGenerator::GetInstance().Initialize();

#ifdef USE_IMGUI
        // ImGuiマネージャークラスの初期化
        imGui_->Initialize(winApp_->GetHwnd(), GetComponent<DirectXCommon>());

        // GPU タイムスタンププロファイラーの初期化
        gpuProfiler_.Initialize(GetComponent<DirectXCommon>()->GetDevice());

        // ゲームデバッグUIの初期化（DockingUIを渡す）
        gameDebugUI_->Initialize(this, imGui_->GetDockingUI());

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
            if (auto* mm = GetComponent<ModelManager>()) { return mm->GetThreadPool(); }
            return nullptr;
            });
        gameDebugUI_->RegisterEnginePanel("Thread Profiler", [this]() {
            threadProfilerUI_->Draw();
            });

        // キーコンフィグUIの登録
        gameDebugUI_->RegisterEnginePanel("Key Config", [this]() {
            if (auto* inputManager = GetComponent<InputManager>()) {
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

            // パーティクルシステムデバッグを右側に配置
            dockingUI->RegisterWindow("Particle System Debug", DockArea::Right);
        }
#endif // USE_IMGUI

        GameObject::SetEngine(this);

        // デフォルトレンダーパイプラインの構築
        BuildDefaultRenderPipeline();
    }

    void EngineSystem::Finalize()
    {
#ifdef USE_IMGUI
        // コンソールUIへのログ転送を解除（ImGui解放前に行う）
        Logger::GetInstance().ClearConsoleCallback();

        // プロファイラーの終了処理（ImGui より先に解放）
        gpuProfiler_.Finalize();

        // ImGuiの終了処理
        imGui_->Finalize();
#endif // USE_IMGUI

        // TextureManagerのキャッシュをクリア
        TextureManager::GetInstance().Clear();

        // AssetDatabaseの終了処理
        AssetDatabase::GetInstance().Finalize();

        componentOwners_.clear();

        // COMの解放
        CoUninitialize();

        // 非同期ロガーを明示的に停止し、終了時の待ち状態を防ぐ。
        Logger::GetInstance().Shutdown();
    }

    void EngineSystem::BeginFrame()
    {
        // フレームレート制御の開始
        if (auto* frameRate = GetComponent<FrameRateController>()) {
            frameRate->BeginFrame();
        }

        // RenderManagerの描画キューをクリア（前フレームのコマンドを削除）
        if (auto* renderManager = GetComponent<RenderManager>()) {
            renderManager->ClearQueue();
        }

        // 入力の更新
        if (auto* inputManager = GetComponent<InputManager>()) {
            inputManager->Update();
        }

        // ポストエフェクトの更新（フレームレートコントローラーからデルタタイムを取得）
        if (auto* postEffect = GetComponent<PostEffectManager>()) {
            if (auto* frameRate = GetComponent<FrameRateController>()) {
                postEffect->Update(frameRate->GetDeltaTime());
            }
        }

#ifdef USE_IMGUI
        if (sceneManager_) {
            if (auto* sceneViewport = imGui_->GetSceneViewport()) {
                sceneViewport->SetCamera(sceneManager_->GetSceneViewCamera());
                sceneViewport->SetGameCamera3D(sceneManager_->GetGameViewCamera3D());
                sceneViewport->SetCamera2D(sceneManager_->GetGameViewCamera2D());
            }
        }

        // SceneViewportにInputQueryを渡す（ギズモ操作のキーコンフィグ対応）
        if (auto* sceneViewport = imGui_->GetSceneViewport()) {
            if (auto* inputManager = GetComponent<InputManager>()) {
                sceneViewport->SetInputQuery(&inputManager->GetQuery());
            }
        }

        // ImGuiの開始（PostEffectManagerとGameDebugUIを渡す）
        if (auto* postEffect = GetComponent<PostEffectManager>()) {
            imGui_->Begin(postEffect, GetComponent<Render>(), gameDebugUI_.get());
        }

        //メニューバーを最初に描画（ドッキングスペースより前）
        gameDebugUI_->ShowMainMenuBar();

        // その他のデバッグUIの更新（メニューバー以外）
        gameDebugUI_->UpdateDebugPanels();

        if (sceneManager_) {
            if (auto* sceneViewport = imGui_->GetSceneViewport()) {
                if (auto* gameObjectManager = sceneManager_->GetCurrentGameObjectManager()) {
                    if (auto* sceneCamera = sceneManager_->GetSceneViewCamera()) {
                        sceneViewport->UpdateObjectSelection(gameObjectManager, sceneCamera);
                    }
                    if (auto* camera2D = sceneManager_->GetGameViewCamera2D()) {
                        sceneViewport->UpdateSpriteSelection(gameObjectManager, camera2D);
                    }
                }
            }
        }
#endif // USE_IMGUI
    }

    void EngineSystem::EndFrame()
    {
#ifdef USE_IMGUI
        imGui_->End();
#endif // USE_IMGUI

        // VSync有効時はフレームレート制御の終了処理は不要
        // Present(1, 0)が自動的に60Hzに同期してくれる
    }

    void EngineSystem::ExecuteRenderPipeline(std::function<void()> renderCallback)
    {
        if (!renderPipeline_) {
            return;
        }

        auto* dx = GetComponent<DirectXCommon>();
        auto* renderManager = GetComponent<RenderManager>();
        auto* render = GetComponent<Render>();

        // コマンドリストを設定
        if (renderManager && dx) {
            renderManager->SetCommandList(dx->GetCommandList());
        }

        // レンダリングコンテキストの構築
        RenderContext context;
        context.dxCommon = dx;
        context.renderManager = renderManager;
        context.postEffectManager = GetComponent<PostEffectManager>();
        context.lightManager = GetComponent<LightManager>();
        context.gBufferManager = dx ? dx->GetGBufferManager() : nullptr;
        context.shadowMapManager = dx ? dx->GetShadowMapManager() : nullptr;  // DeferredLighting でシャドウ/LVP に使用
        context.accelerationStructureManager = dx ? dx->GetAccelerationStructureManager() : nullptr;
        context.rtShadowManager = dx ? dx->GetRayTracingShadowManager() : nullptr;

        // RenderTargetManagerを設定（Phase 1で追加）
        if (render) {
            context.renderTargetManager = render->GetRenderTargetManager();
        }

        // ジオメトリパスに描画コールバックを設定する
        // GBufferPass + DeferredLightingPass が有効なため、不透明 Model/SkinnedModel は GeometryPass でクリアしない
        if (auto* geometryPass = renderPipeline_->GetPass<GeometryPass>()) {
            geometryPass->SetRenderCallback(renderCallback);

            // DeferredLightingPass が Offscreen0 に書き込んだ結果を保持するためクリアを無効にする
            // GeometryPass はその上に透過オブジェクト / SkyBox / UI 等を重ねて描画する
            const bool deferredEnabled = renderPipeline_->GetPass<DeferredLightingPass>()
                && renderPipeline_->GetPass<DeferredLightingPass>()->IsEnabled();
            geometryPass->SetClearEnabled(!deferredEnabled);
        }

        // DeferredLightingPass が有効な場合は、RenderManager の Forward パスで
        // 不透明 Model/SkinnedModel を二重描画しないようスキップフラグを立てる
        if (renderManager) {
            const bool deferredEnabled = renderPipeline_->GetPass<DeferredLightingPass>()
                && renderPipeline_->GetPass<DeferredLightingPass>()->IsEnabled();
            renderManager->SetSkipOpaqueMeshInForwardPass(deferredEnabled);
        }

        PassOutput previousOutput{};
        auto executePass = [&](RenderPass* pass) {
            if (!pass || !pass->IsEnabled()) {
                return;
            }

            if (previousOutput.isValid) {
                pass->SetInput(previousOutput);
            }

            pass->Setup(context);
            pass->Execute(context);
            pass->Cleanup(context);
            previousOutput = pass->GetOutput();
            };

#ifdef USE_IMGUI
        // フレームインデックスを取得してプロファイラーをリセット
        const UINT currentFrameIndex = dx ? dx->GetSwapChain()->GetCurrentBackBufferIndex() : 0;
        ID3D12GraphicsCommandList* cmdList = dx ? dx->GetCommandList() : nullptr;
        gpuProfiler_.NewFrame(currentFrameIndex);

        // フレーム全体の GPU / CPU 計測開始
        gpuProfiler_.BeginCpuTimestamp(GpuTimestampSlot::Total);
        gpuProfiler_.BeginGpuTimestamp(GpuTimestampSlot::Total, cmdList);
#endif

        // DXR BLAS 構築（未構築のモデルリソースを遅延ビルド）
        if (auto* asMgr = context.accelerationStructureManager) {
            if (asMgr->IsSupported()) {
                // フレーム開始時に RT シャドウの状態をリセット
                if (context.rtShadowManager) {
                    context.rtShadowManager->ResetFrameState();
                }
                if (auto* modelManager = GetComponent<ModelManager>()) {
                    auto* cmdListForBLAS = dx->GetCommandList();
                    modelManager->ForEachResource([&](ModelResource* resource) {
                        asMgr->BuildBLASFromModelResource(cmdListForBLAS, resource);
                        });
                }

                // DXR TLAS 構築（シーン内の全 ModelGameObject からインスタンスを収集）
                if (sceneManager_) {
                    if (auto* objMgr = sceneManager_->GetCurrentGameObjectManager()) {
                        std::vector<AccelerationStructureManager::InstanceDesc> tlasInstances;

                        for (auto& obj : objMgr->GetAllObjects()) {
                            if (!obj || !obj->IsActive()) continue;

                            // ModelGameObject にダウンキャスト
                            auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get());
                            if (!modelObj) continue;

                            auto* model = modelObj->GetModel();
                            if (!model) continue;

                            auto* resource = model->GetModelResource();
                            if (!resource || !resource->HasBLAS()) continue;

                            AccelerationStructureManager::InstanceDesc inst;
                            inst.blasIndex = resource->GetBLASIndex();
                            inst.SetTransform(modelObj->GetTransform().GetWorldMatrix());
                            tlasInstances.push_back(inst);
                        }

                        if (!tlasInstances.empty()) {
                            asMgr->BuildTLAS(dx->GetCommandList(), tlasInstances);
                        }
                    }
                }
            }
        }

        // Shadow Pass
        {
#ifdef USE_IMGUI
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::ShadowPass, cmdList);
#endif
            executePass(renderPipeline_->GetPass<ShadowMapPass>());
        }

#ifdef USE_IMGUI
        // DXR レイトレーシングシャドウのディスパッチ（全ディレクショナルライト分）
        auto dispatchRTShadow = [&](RayTracingShadowManager::ViewID viewId) {
            auto* rtShadow = context.rtShadowManager;
            if (!rtShadow || !rtShadow->IsInitialized()) return;
            if (!context.gBufferManager || !context.lightManager) return;

            auto* worldPosResource = context.gBufferManager->GetResource(GBufferManager::Target::WorldPosition);
            auto* normalResource = context.gBufferManager->GetResource(GBufferManager::Target::NormalRoughness);
            auto* motionVecResource = context.gBufferManager->GetResource(GBufferManager::Target::MotionVector);

            // GBufferManager が管理するステートを直接参照して冗長バリアを防ぐ
            auto& worldPosState = context.gBufferManager->GetCurrentState(GBufferManager::Target::WorldPosition);
            auto& normalState = context.gBufferManager->GetCurrentState(GBufferManager::Target::NormalRoughness);
            auto& motionVecState = context.gBufferManager->GetCurrentState(GBufferManager::Target::MotionVector);

            ResourceBarrierHelper::Transition(cmdList, worldPosResource, worldPosState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ResourceBarrierHelper::Transition(cmdList, normalResource, normalState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ResourceBarrierHelper::Transition(cmdList, motionVecResource, motionVecState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            auto worldPosSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::WorldPosition);
            auto normalSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
            auto motionVecSRV = context.gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector);

            const uint32_t maxLights = RayTracingShadowManager::kMaxDirectionalLights;

            // 全ライト分 DispatchRays を先に実行（GPU パイプラインを詰めるため）
            for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
                auto* dirLight = context.lightManager->GetDirectionalLight(li);
                if (!dirLight || !dirLight->enabled) continue;

                rtShadow->Dispatch(
                    dx->GetCommandList(),
                    worldPosSRV,
                    normalSRV,
                    motionVecSRV,
                    dirLight->direction,
                    static_cast<UINT>(dx->GetClientWidth()),
                    static_cast<UINT>(dx->GetClientHeight()),
                    viewId,
                    li);
            }

            // 全ライト分 A-Trous デノイズをまとめて実行
            for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
                auto* dirLight = context.lightManager->GetDirectionalLight(li);
                if (!dirLight || !dirLight->enabled) continue;

                rtShadow->Denoise(
                    dx->GetCommandList(),
                    normalSRV,
                    worldPosSRV,
                    static_cast<UINT>(dx->GetClientWidth()),
                    static_cast<UINT>(dx->GetClientHeight()),
                    viewId,
                    li);
            }

            // DeferredLightingPass が PIXEL_SHADER_RESOURCE として読み取るために戻す
            ResourceBarrierHelper::Transition(cmdList, worldPosResource, worldPosState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            ResourceBarrierHelper::Transition(cmdList, normalResource, normalState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            ResourceBarrierHelper::Transition(cmdList, motionVecResource, motionVecState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            };

        // SceneView 再描画判定:
        // - Scene カメラが動いた → 即時再描画（インタラクティブな操作をフレーム遅延なく反映）
        // - カメラ静止中 → kSceneViewMaxSkipFrames フレームに 1 回再描画（アニメーション対応）
        // 静止中は前フレームの SceneView RT をそのまま表示継続し、GPU 再描画をスキップする。
        // SceneView は GBufferPass より前に実行（共有 DSV のクリア競合回避）。
        bool sceneViewDirty = false;
        if (imGui_->IsSceneViewVisible() && sceneManager_) {
            if (const ICamera* sceneCamera = sceneManager_->GetSceneViewCamera()) {
                const Matrix4x4& currentMat = sceneCamera->GetViewMatrix();
                if (std::memcmp(&currentMat, &prevSceneCameraViewMatrix_, sizeof(Matrix4x4)) != 0) {
                    prevSceneCameraViewMatrix_ = currentMat;
                    sceneViewSkipCounter_ = 0;
                    sceneViewDirty = true;
                }
            }
            if (!sceneViewDirty && ++sceneViewSkipCounter_ >= kSceneViewMaxSkipFrames) {
                sceneViewSkipCounter_ = 0;
                sceneViewDirty = true;
            }
        }

        if (sceneManager_ && render && sceneViewDirty) {
            if (auto* sceneViewTarget = render->GetRenderTarget("SceneView")) {
                GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::SceneView, cmdList);

                // ゲームパスのために previousOutput を保存（SceneView パス後に復元）
                PassOutput savedOutput = previousOutput;

                // SceneView カメラ・レンダリング状態をセットアップ
                sceneManager_->SetupSceneViewCamera();

                // 1. GBufferPass: 不透明オブジェクトを G-Buffer に書き込む（Scene カメラ使用）
                executePass(renderPipeline_->GetPass<GBufferPass>());

                // SceneView は RTシャドウを使わない（重いためエディタビューでは省略）
                // 代わりに ShadowMap ベースのシャドウのみ使用
                context.currentRTShadowViewId = static_cast<uint32_t>(RayTracingShadowManager::ViewID::SceneView);

                // 2. DeferredLightingPass: G-Buffer を読み取り PBR+IBL ライティングを計算
                context.currentRTShadowViewId = static_cast<uint32_t>(RayTracingShadowManager::ViewID::SceneView);
                executePass(renderPipeline_->GetPass<DeferredLightingPass>());

                // 3. GeometryPass: スカイボックス・グリッド・透過オブジェクトを重ねて描画
                if (auto* geometryPass = renderPipeline_->GetPass<GeometryPass>()) {
                    geometryPass->SetRenderCallback([this]() { sceneManager_->DrawSceneViewGeometry(); });
                }
                executePass(renderPipeline_->GetPass<GeometryPass>());
                // ゲームビュー用コールバックに戻す
                if (auto* geometryPass = renderPipeline_->GetPass<GeometryPass>()) {
                    geometryPass->SetRenderCallback(renderCallback);
                }

                // 4. ポストエフェクトチェーンを適用し、結果を SceneView RT にコピー
                if (auto* postEffect = GetComponent<PostEffectManager>()) {
                    auto resultHandle = postEffect->ExecuteEffectChain(previousOutput.srvHandle);
                    sceneViewTarget->Begin(cmdList);
                    postEffect->ExecuteEffect("FullScreen", resultHandle);
                    sceneViewTarget->End(cmdList);
                }

                // SceneView カメラ・レンダリング状態を復元
                sceneManager_->RestoreGameViewCamera();

                // ゲームパス用に previousOutput を復元
                previousOutput = savedOutput;
            }
        }
#endif // USE_IMGUI

        {
#ifdef USE_IMGUI
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::GBufferPass, cmdList);
#endif
            executePass(renderPipeline_->GetPass<GBufferPass>());
        }

        // Game ビュー用 RT シャドウディスパッチ
        dispatchRTShadow(RayTracingShadowManager::ViewID::GameView);

        {
#ifdef USE_IMGUI
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::DeferredLighting, cmdList);
#endif
            context.currentRTShadowViewId = static_cast<uint32_t>(RayTracingShadowManager::ViewID::GameView);
            executePass(renderPipeline_->GetPass<DeferredLightingPass>());
        }

        {
#ifdef USE_IMGUI
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::GeometryPass, cmdList);
#endif
            executePass(renderPipeline_->GetPass<GeometryPass>());
        }

        {
#ifdef USE_IMGUI
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::PostEffect, cmdList);
#endif
            executePass(renderPipeline_->GetPass<PostEffectPass>());
        }
        {
#ifdef USE_IMGUI
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::BackBufferPass, cmdList);
#endif
            executePass(renderPipeline_->GetPass<BackBufferPass>());
        }

#ifdef USE_IMGUI
        // ImGuiの描画コマンドを積む
        {
            GpuTimestampProfiler::ProfileScope scope(gpuProfiler_, GpuTimestampSlot::ImGuiDraw, cmdList);
            if (imGui_) {
                imGui_->Draw();
            }
        }

        // フレーム全体の GPU / CPU 計測終了 → コマンドリストを Close する前に解決
        gpuProfiler_.EndCpuTimestamp(GpuTimestampSlot::Total);
        gpuProfiler_.EndGpuTimestamp(GpuTimestampSlot::Total, cmdList);
        gpuProfiler_.ResolveAll(cmdList, currentFrameIndex);
#endif // USE_IMGUI

        // フレームの最終処理（バックバッファ終了、コマンド実行、Present）
        if (render) {
            render->FinalizeFrame();
        }

        // GPU 実行完了後に DXR 退避リソースを解放
        if (auto* asMgr = context.accelerationStructureManager) {
            asMgr->FlushRetiredResources();
        }

#ifdef USE_IMGUI
        // FinalizeFrame() 完了後 (WaitForFrame 済み) に前フレームの結果を読み取る
        if (dx) {
            const UINT nextFrameIndex = dx->GetSwapChain()->GetCurrentBackBufferIndex();
            gpuProfiler_.ReadResults(dx->GetCommandQueue(), nextFrameIndex);

            if (auto* dockingUI = GetDockingUI()) {
                dockingUI->SetTimingData(gpuProfiler_.GetResults());
            }
        }
#endif // USE_IMGUI
    }

    // ──────────────────────────────────────────────────────────
    // コンポーネント作成ヘルパーメソッド
    // ──────────────────────────────────────────────────────────

#pragma region コンポーネントヘルパーメソッド

    void EngineSystem::CreateFrameRateController()
    {
        // フレームレート制御を作成・初期化
        auto frameRate = std::make_unique<FrameRateController>();
        frameRate->Initialize(); // 60FPS固定

        // ComponentManagerに登録（所有権を移譲）
        RegisterComponent(std::move(frameRate));
    }

    void EngineSystem::CreateGraphicsComponents(const EngineConfig& config)
    {
        // DirectXCommonの作成と初期化
        auto directXCommon = std::make_unique<DirectXCommon>();
        directXCommon->Initialize(winApp_, config);
        DirectXCommon* dxPtr = directXCommon.get();
        RegisterComponent(std::move(directXCommon));

        // TextureManagerの初期化（シングルトン）
        TextureManager::GetInstance().Initialize(dxPtr);

        // ResourceFactoryの作成（コンストラクタで初期化済み）
        auto resourceFactory = std::make_unique<ResourceFactory>();
        ResourceFactory* resourcePtr = resourceFactory.get();
        RegisterComponent(std::move(resourceFactory));

        // Renderの作成と初期化（DSVヒープが必要）
        auto render = std::make_unique<Render>();
        render->Initialize(dxPtr, dxPtr->GetDSVHeap());
        Render* renderPtr = render.get();
        RegisterComponent(std::move(render));

        // RenderManagerの作成と初期化
        auto renderManager = std::make_unique<RenderManager>();
        renderManager->Initialize(dxPtr->GetDevice());
        RenderManager* renderManagerPtr = renderManager.get();

        // ShadowMapManagerを設定
        renderManager->SetShadowMapManager(dxPtr->GetShadowMapManager());

        // ShadowMapRendererの作成と登録（最優先）
        auto shadowMapRenderer = std::make_unique<ShadowMapRenderer>();
        shadowMapRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::ShadowMap, std::move(shadowMapRenderer));

        // ModelRendererの作成と登録
        auto modelRenderer = std::make_unique<ModelRenderer>();
        modelRenderer->Initialize(dxPtr->GetDevice());

        // シャドウマップSRVを設定
        modelRenderer->SetShadowMap(dxPtr->GetShadowMapSRVHandle());

        renderManager->RegisterRenderer(RenderPassType::Model, std::move(modelRenderer));

        // SkinnedModelRendererの作成と登録
        auto skinnedRenderer = std::make_unique<SkinnedModelRenderer>();
        skinnedRenderer->Initialize(dxPtr->GetDevice());

        // シャドウマップSRVを設定
        skinnedRenderer->SetShadowMap(dxPtr->GetShadowMapSRVHandle());

        renderManager->RegisterRenderer(RenderPassType::SkinnedModel, std::move(skinnedRenderer));

        // SkyBoxRendererの作成と登録
        auto skyBoxRenderer = std::make_unique<SkyBoxRenderer>();
        skyBoxRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::SkyBox, std::move(skyBoxRenderer));

        // SpriteRendererの作成と登録
        auto spriteRenderer = std::make_unique<SpriteRenderer>();
        spriteRenderer->Initialize(dxPtr, resourcePtr);
        renderManager->RegisterRenderer(RenderPassType::Sprite, std::move(spriteRenderer));


        // ParticleRendererの作成と登録
        auto particleRenderer = std::make_unique<ParticleRenderer>();
        particleRenderer->SetResourceFactory(resourcePtr);
        particleRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::Particle, std::move(particleRenderer));

        // ModelParticleRendererの作成と登録
        auto modelParticleRenderer = std::make_unique<ModelParticleRenderer>();
        modelParticleRenderer->SetResourceFactory(resourcePtr);
        modelParticleRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::ModelParticle, std::move(modelParticleRenderer));

        // LineRendererPipelineの作成と登録
        auto lineRendererPipeline = std::make_unique<LineRendererPipeline>();
        lineRendererPipeline->Initialize(dxPtr, resourcePtr);
        LineRendererPipeline* lineRendererPtr = lineRendererPipeline.get();
        renderManager->RegisterRenderer(RenderPassType::Line, std::move(lineRendererPipeline));

        // RenderManagerを登録
        RegisterComponent(std::move(renderManager));


        // LineManagerの初期化（シングルトン、RenderManager登録後に実行）
        LineManager::GetInstance().Initialize(lineRendererPtr);

        // PostEffectManagerの作成と初期化
        auto postEffectManager = std::make_unique<PostEffectManager>();
        postEffectManager->Initialize(dxPtr, renderPtr);
        RegisterComponent(std::move(postEffectManager));

        // ModelManagerの作成と初期化
        auto modelManager = std::make_unique<ModelManager>();
        modelManager->Initialize(dxPtr, resourcePtr);
        RegisterComponent(std::move(modelManager));

        // 全レンダラー登録完了後、ModelManager に 描画依存コンテキストを設定
        // （Model インスタンス生成時に各 Model へ注入される）
        ModelRenderContext modelCtx;
        modelCtx.dxCommon = dxPtr;
        modelCtx.shadowMapManager = dxPtr->GetShadowMapManager();
        modelCtx.modelRenderer = dynamic_cast<BaseModelRenderer*>(renderManagerPtr->GetRenderer(RenderPassType::Model));
        modelCtx.skinnedRenderer = dynamic_cast<BaseModelRenderer*>(renderManagerPtr->GetRenderer(RenderPassType::SkinnedModel));
        modelCtx.shadowRenderer = static_cast<ShadowMapRenderer*>(renderManagerPtr->GetRenderer(RenderPassType::ShadowMap));
        GetComponent<ModelManager>()->SetRenderContext(modelCtx);

        // IBLGeneratorの作成と初期化
        auto iblGenerator = std::make_unique<IBLGenerator>();
        IBLGenerator* iblGeneratorPtr = iblGenerator.get();
        auto shaderCompiler = std::make_unique<ShaderCompiler>();
        shaderCompiler->Initialize();
        iblGenerator->Initialize(dxPtr, shaderCompiler.get());
        RegisterComponent(std::move(iblGenerator));
        RegisterComponent(std::move(shaderCompiler));

        auto iblSystem = std::make_unique<IBLSystem>();
        if (!iblSystem->Initialize(dxPtr, iblGeneratorPtr, renderManagerPtr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to initialize IBLSystem");
        }
        RegisterComponent(std::move(iblSystem));
    }

    void EngineSystem::CreateInputComponents()
    {
        auto inputManager = std::make_unique<InputManager>();
        inputManager->Initialize(winApp_->GetInstance(), winApp_->GetHwnd());
        RegisterComponent(std::move(inputManager));
    }

    void EngineSystem::CreateAudioComponents()
    {
        // SoundManagerの作成と初期化
        auto soundManager = std::make_unique<SoundManager>();
        soundManager->Initialize();
        RegisterComponent(std::move(soundManager));
    }

    void EngineSystem::CreateLightComponents()
    {
        auto lightManager = std::make_unique<LightManager>();
        auto* dxCommon = GetComponent<DirectXCommon>();
        auto* resourceFactory = GetComponent<ResourceFactory>();
        auto* descriptorManager = dxCommon->GetDescriptorManager();

        lightManager->Initialize(
            dxCommon->GetDevice(),
            resourceFactory,
            descriptorManager
        );

        // デフォルトライトは作成しない（各シーンで個別に作成する）

        LightManager* lightManagerPtr = lightManager.get();
        RegisterComponent(std::move(lightManager));

        // RenderManagerにLightManagerを設定
        auto* renderManager = GetComponent<RenderManager>();
        if (renderManager) {
            // Model / SkinnedModel の両レンダラーに LightManager を一括設定
            for (auto passType : { RenderPassType::Model, RenderPassType::SkinnedModel }) {
                if (auto* r = dynamic_cast<BaseModelRenderer*>(renderManager->GetRenderer(passType))) {
                    r->SetLightManager(lightManagerPtr);
                }
            }
        }
    }

    void EngineSystem::BuildDefaultRenderPipeline()
    {
        // レンダーパイプラインの作成
        renderPipeline_ = std::make_unique<RenderPipeline>();

        // 1. シャドウマップパス
        auto shadowMapPass = std::make_unique<ShadowMapPass>();
        renderPipeline_->AddPass(std::move(shadowMapPass));

        // 2. G-Bufferパス（不透明 Model / SkinnedModel の蓄積）
        auto gBufferPass = std::make_unique<GBufferPass>();
        renderPipeline_->AddPass(std::move(gBufferPass));

        // 3. DeferredLightingパス
        // G-Buffer (AlbedoAO / NormalRoughness / EmissiveMetallic) を読み取り、
        // 簡易ライティングを計算して Offscreen0 に書き込む
        auto deferredLightingPass = std::make_unique<DeferredLightingPass>();
        deferredLightingPass->SetRenderTargetName("Offscreen0");
        renderPipeline_->AddPass(std::move(deferredLightingPass));

        // 4. ジオメトリパス（透過オブジェクト / SkyBox / UI / パーティクル 等の Forward 描画）
        // DeferredLightingPass が Offscreen0 に書き込んだ結果の上に重ね描きする
        // 不透明 Model/SkinnedModel は GBufferPass + DeferredLightingPass で処理済みなので描画しない
        auto geometryPass = std::make_unique<GeometryPass>();
        geometryPass->SetRenderTargetName("Offscreen0");  // 名前ベースで指定
        renderPipeline_->AddPass(std::move(geometryPass));

        // 5. ポストエフェクトパス
        auto postEffectPass = std::make_unique<PostEffectPass>();
        renderPipeline_->AddPass(std::move(postEffectPass));

        // 6. バックバッファパス（最終出力）
        auto backBufferPass = std::make_unique<BackBufferPass>();
        backBufferPass->SetRenderTargetName("BackBuffer");  // 名前ベースで指定
        renderPipeline_->AddPass(std::move(backBufferPass));
    }

#pragma endregion
}
