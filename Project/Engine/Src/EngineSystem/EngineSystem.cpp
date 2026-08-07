#include "pch.h"
#include "EngineSystem.h"
#include "Subsystem/RayTracingSubsystem.h"
#ifdef USE_IMGUI
#include "Settings/EditorSettingsSubsystem.h"
#endif
#include "Factory/GraphicsComponentFactory.h"
#include "Factory/CoreComponentFactory.h"
#include <cstring>

// ユーティリティ
#include "Utility/Random/RandomGenerator.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Asset/AssetDatabase.h"

// EngineSystem が直接使う型
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/FrameRateController.h"

#if defined(USE_IMGUI) && defined(USE_PIX)
#include "Editor/ImGui/PixCapture.h"
#endif

// レンダーパス
#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Graphics/Render/Pass/ASBuildPass.h"
#include "Graphics/Render/Pass/GBufferPass.h"
#include "Graphics/Render/Pass/HiZOcclusionPass.h"
#include "Graphics/Render/Pass/SSAOPass.h"
#include "Graphics/Render/Pass/TAAPass.h"
#include "Graphics/Render/Pass/CASPass.h"
#include "Graphics/Render/Pass/DeferredLightingPass.h"
#include "Graphics/Render/Pass/RTShadowPass.h"
#include "Graphics/Render/Pass/RTWaterCausticsPass.h"
#include "Graphics/Render/Pass/RTWaterRefractionPass.h"
#include "Graphics/Render/Pass/RTWaterReflectionPass.h"
#include "Graphics/Render/Pass/FFTOceanPass.h"
#include "Graphics/Render/Pass/AtmosphereLUTPass.h"
#include "Graphics/Render/Pass/AerialPerspectivePass.h"
#include "Graphics/Render/Pass/VolumetricCloudNoisePass.h"
#include "Graphics/Render/Pass/VolumetricCloudPass.h"
#include "Graphics/Render/Pass/GodRayPass.h"
#include "Graphics/Render/Pass/WaterCausticsPass.h"
#include "Graphics/Render/Pass/GeometryPass.h"
#include "Graphics/Render/Pass/SceneColorCopyPass.h"
#include "Graphics/Render/Pass/WaterSurfacePass.h"
#include "Graphics/Render/Pass/PostEffectPass.h"
#include "Graphics/Render/Pass/BackBufferPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Scene/IScene.h"

// Hi-Z オクルージョンカリング
#include "Graphics/Render/Culling/HiZOcclusionSystem.h"

// レイトレーシング
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"

#include "GameObject/GameObject.h"
#include "Scene/SceneManager.h"
#include "Camera/View/ViewInfo.h"
#include "EngineSystem/EngineConfig.h"


namespace CoreEngine
{
    EngineSystem::EngineSystem() = default;
    EngineSystem::~EngineSystem() = default;

    void EngineSystem::SetSceneManager(SceneManager* sceneManager)
    {
        componentManager_.Register<SceneManager>(sceneManager);
    }

    SceneManager* EngineSystem::GetSceneManager() const
    {
        return componentManager_.Get<SceneManager>();
    }

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

        // ──────────────────────────────────────────────────────────
        // サブシステム登録 + 一括初期化
        // ──────────────────────────────────────────────────────────
        {
            subsystems_.push_back(std::make_unique<RayTracingSubsystem>());
        }
#ifdef USE_IMGUI
        {
            // エディタ設定の自動保存（セクション登録元より先に生成しておく）
            subsystems_.push_back(std::make_unique<EditorSettingsSubsystem>());
            subsystems_.push_back(std::make_unique<DebugSubsystem>());
        }
#endif // USE_IMGUI

        for (auto& sys : subsystems_) {
            sys->Initialize(this, config);
        }

        GameObject::SetEngine(this);

        // デフォルトレンダーパイプラインの構築
        BuildDefaultRenderPipeline();
    }

    void EngineSystem::Finalize()
    {
        // サブシステムを登録の逆順で終了処理
        for (auto it = subsystems_.rbegin(); it != subsystems_.rend(); ++it) {
            (*it)->Finalize();
        }
        subsystems_.clear();

        // TextureManager
        TextureManager::GetInstance().Clear();

        // AssetDatabaseの終了処理
        AssetDatabase::GetInstance().Finalize();

        // Hi-Z オクルージョンカリングの GPU リソースを解放する
        // （DirectXCommon 破棄前に明示解放しないと LeakChecker の ReportLiveObjects に報告される。
        //   インスタンス自体は ~ModelVisibility の UnregisterTarget が空振りできるよう
        //   ここでは reset せず、EngineSystem のデストラクタまで生存させる）
        if (hiZOcclusionSystem_) {
            hiZOcclusionSystem_->Shutdown();
        }

        // RenderDomainContext を先にシャットダウンしてから DirectXCommon を解放する
        if (renderDomainContext_) {
            renderDomainContext_->Shutdown();
            renderDomainContext_.reset();
        }

        renderPipeline_.reset();

        while (!componentOwners_.empty()) {
            componentOwners_.back().reset();
            componentOwners_.pop_back();
        }
        componentManager_.Clear();

        // COMの解放
        CoUninitialize();

        // 非同期ロガーを明示的に停止し、終了時の待ち状態を防ぐ。
        Logger::GetInstance().Shutdown();
    }

    void EngineSystem::BeginFrame()
    {
        // フレームレート制御の開始
        if (auto* frameRate = GetService<FrameRateController>()) {
            frameRate->BeginFrame();
        }

        // RenderManagerの描画キューをクリア（前フレームのコマンドを削除）
        if (auto* renderManager = GetService<RenderManager>()) {
            renderManager->ClearQueue();
        }

        // 入力の更新
        //（Esc による終了は WinApp::WindowProc / GameOutputWindow::WindowProc が
        //  ウィンドウメッセージとして処理する。DirectInput は DISCL_FOREGROUND で
        //  本体ウィンドウに結び付いており、別ウィンドウにフォーカスがある間は拾えないため）
        if (auto* inputManager = GetService<InputManager>()) {
            inputManager->Update();
        }

        // ポストエフェクトの更新（フレームレートコントローラーからデルタタイムを取得）
        if (auto* postEffect = GetService<PostEffectManager>()) {
            if (auto* frameRate = GetService<FrameRateController>()) {
                postEffect->Update(frameRate->GetDeltaTime());
            }
        }

        // 全サブシステムのフレーム開始処理
        for (auto& sys : subsystems_) {
            sys->BeginFrame();
        }
    }

    void EngineSystem::EndFrame()
    {
        // 全サブシステムのフレーム終了処理（登録の逆順）
        for (auto it = subsystems_.rbegin(); it != subsystems_.rend(); ++it) {
            (*it)->EndFrame();
        }

        // VSync有効時はフレームレート制御の終了処理は不要
        // Present(1, 0)が自動的に60Hzに同期してくれる
    }

    void EngineSystem::ExecuteRenderPipeline()
    {
        if (!renderPipeline_) {
            return;
        }

        // サブシステムキャッシュ（フレーム内再利用）
        auto* rayTracing = GetSubsystem<RayTracingSubsystem>();
#ifdef USE_IMGUI
        auto* debug = GetSubsystem<DebugSubsystem>();
#endif

        auto* dx = GetService<DirectXCommon>();
        auto* renderManager = GetService<RenderManager>();
        auto* render = GetService<Render>();
        auto* sceneManager = GetService<SceneManager>();

        // コマンドリストを設定
        if (renderManager && dx) {
            renderManager->SetCommandList(dx->GetCommandList());
        }

        // レンダリングコンテキストの構築
        FrameBlackboard frameBlackboard;
        RenderContext context;
        context.dxCommon = dx;
        context.renderManager = renderManager;
        context.rayTracingSubsystem = rayTracing;
        context.sceneManager = sceneManager;
        context.postEffectManager = GetService<PostEffectManager>();
        context.renderingTechniqueManager = GetService<RenderingTechniqueManager>();
        context.lightManager = GetService<LightManager>();
        context.gBufferManager = renderDomainContext_ ? renderDomainContext_->GetGBufferManager() : nullptr;
        context.accelerationStructureManager = renderDomainContext_ ? renderDomainContext_->GetAccelerationStructureManager() : nullptr;
        context.rtShadowManager = renderDomainContext_ ? renderDomainContext_->GetRayTracingShadowManager() : nullptr;
        context.rtWaterCausticsManager = renderDomainContext_ ? renderDomainContext_->GetWaterCausticsRayTracingManager() : nullptr;
        context.rtWaterRefractionManager = renderDomainContext_ ? renderDomainContext_->GetWaterRefractionRayTracingManager() : nullptr;
        context.rtWaterReflectionManager = renderDomainContext_ ? renderDomainContext_->GetWaterReflectionRayTracingManager() : nullptr;
        context.fftOceanManager = renderDomainContext_ ? renderDomainContext_->GetFFTOceanManager() : nullptr;
        context.atmosphereManager = renderDomainContext_ ? renderDomainContext_->GetAtmosphereManager() : nullptr;
        context.volumetricCloudManager = renderDomainContext_ ? renderDomainContext_->GetVolumetricCloudManager() : nullptr;
        context.depthStencilManager = dx ? dx->GetDepthStencilManager() : nullptr;
        context.frameBlackboard = &frameBlackboard;
        context.modelManager = GetService<ModelManager>();
        // 水面状態は WaterRenderFeature が RenderDomainContext へ publish する
        // （シーンに水面用の仮想関数を持たせない）
        context.waterSurfaceState = renderDomainContext_ ? renderDomainContext_->GetWaterSurfaceState() : nullptr;
        context.fftOceanSimulationTime = renderDomainContext_ ? renderDomainContext_->GetFFTOceanSimulationTime() : 0.0f;
        context.frameNumber = ++renderFrameNumber_;
#ifdef USE_IMGUI
        // RenderGraph 内の各パスが自動でタイミング計測できるようプロファイラを渡す
        // （nullptr の場合 RenderGraph::Execute は計測をスキップする）
        context.gpuProfiler = debug ? &debug->GetGpuProfiler() : nullptr;
#endif

        // RenderTargetManager はビュー確定より前に必要（TAA 履歴ターゲット等の判定に使う）
        if (render) {
            context.renderTargetManager = render->GetRenderTargetManager();
        }

        // ===== 今フレームのビューを確定する =====
        // ここで作った ViewInfo が「フレーム内の唯一の真実」になる。以降、描画・カリング・
        // 深度復元・RT はカメラを直接読まず frameViews から行列を取る。
        FrameViews frameViews;
        renderPipeline_->PrepareFrameViews(context, frameViews);
        context.frameViews = &frameViews;
        if (renderManager) {
            renderManager->SetFrameViews(&frameViews);
        }

        if (dx) {
            frameBlackboard.SetResource(
                FrameBlackboard::SceneDepth,
                dx->GetDepthStencilSRV(),
                dx->GetDepthStencilResource(),
                context.depthStencilManager ? &context.depthStencilManager->GetCurrentState() : nullptr);
        }

        // コマンドリストは debug->BeginRenderPipeline 等へ渡すため USE_IMGUI 外で宣言する。
        // USE_IMGUI 無効（Release）ではこれらの呼び出し自体が消えて未使用になるため、
        // maybe_unused を付ける（Release は TreatWarningAsError なので C4189 でビルドが止まる）。
        [[maybe_unused]] ID3D12GraphicsCommandList* cmdList = dx ? dx->GetCommandList() : nullptr;

#ifdef USE_IMGUI
        // プロファイラのリングスロットは記録中フレームインデックスに合わせる
        // （スワップチェーンのインデックスはリサイズで 0 にリセットされるため使わない）
        const UINT currentFrameIndex =
            (dx && dx->GetCommandManager()) ? dx->GetCommandManager()->GetRecordingFrameIndex() : 0;
        if (debug) debug->BeginRenderPipeline(cmdList, currentFrameIndex);
#endif

        // Hi-Z オクルージョンカリング: 完了済みリングスロットの可視性 Readback を反映する。
        // AABB 収集と遮蔽スキップの適用はメイン GameView の構築中のみ有効化する
        // （補助ビュー・反射ビューはカメラが異なり、メインカメラ基準の判定は誤カリングになる）。
        HiZOcclusionSystem* hiZOcclusion = hiZOcclusionSystem_.get();
        assert(hiZOcclusion && "HiZOcclusionSystem must be created by GraphicsComponentFactory");
        hiZOcclusion->BeginFrame(
            (dx && dx->GetCommandManager()) ? dx->GetCommandManager()->GetRecordingFrameIndex() : 0u);
        hiZOcclusion->SetCollectEnabled(false);

        // DXR BLAS / TLAS 構築は ASBuildPass（FrameSetup フェーズ）として
        // 最初に実行される View の RenderGraph 内で行われる。

        // 補助 RenderView は Scene からの要求リストとして受け取り、RenderGraph 単位で順に実行する。
        if (sceneManager && render) {
            std::vector<RenderViewRequest> renderViewRequests = sceneManager->BuildRenderViewRequests();
            for (RenderViewRequest& renderViewRequest : renderViewRequests) {
                if (!renderViewRequest.isEnabled) {
                    continue;
                }

                RenderContext renderViewContext = context;
                renderViewContext.viewSettings = renderViewRequest.viewSettings;
                if (renderViewContext.viewSettings.sceneColorTargetName.empty()) {
                    renderViewContext.viewSettings.sceneColorTargetName = RenderTargetNames::SceneColor;
                }

                // 補助 View のパスはメイン View と同名のため、計測スロット名を
                // View 名で分離する（同名スロット共有だと後続 View がクエリを上書きし、
                // 補助 View 分の GPU 時間が計測から消える）。
                renderViewContext.viewSettings.viewName =
                    !renderViewRequest.name.empty() ? renderViewRequest.name : "AuxView";

                renderViewContext.currentRTShadowViewId =
                    (renderViewContext.viewSettings.viewType == RenderViewType::ReflectionView)
                    ? static_cast<uint32_t>(RayTracingShadowManager::ViewID::ReflectionView)
                    : static_cast<uint32_t>(RayTracingShadowManager::ViewID::GameView);

                const RenderViewResult renderViewResult = renderPipeline_->ExecuteRenderView(
                    renderViewContext,
                    renderViewRequest.beforeExecute,
                    renderViewRequest.afterExecute);

                if (renderViewResult.isValid && renderViewRequest.completionCallback) {
                    renderViewRequest.completionCallback(renderViewResult);
                }
            }
        }

        context.currentRTShadowViewId = static_cast<uint32_t>(RayTracingShadowManager::ViewID::GameView);

        // GameView の主要描画は ShadowMap を含む RenderGraph へ統一して実行する。
        // パス別のタイミングは RenderGraph::Execute が各パス名で自動計測する
        // （EngineProfileScope でまとめて計測すると個別パスの内訳が失われるため使わない）。
        hiZOcclusion->SetCollectEnabled(true);
        renderPipeline_->ExecuteView(context);
        hiZOcclusion->SetCollectEnabled(false);

        // 全 View の描画（AerialPerspective 合成を含む）が完了したので大気有効化フラグを落とす。
        // 次フレームは Update() を呼ぶ大気シーンでのみ再度有効化され、他シーンへの漏れ出しを防ぐ。
        if (context.atmosphereManager) {
            // 大気が要求されなかったフレームは太陽ライトの透過率変調も解除する
            // （大気シーンからキューブマップ空へ切り替えた際に減衰が残らないように。
            //   大気アクティブ時は AtmosphereManager::Update() が毎フレーム上書きする）
            if (!context.atmosphereManager->IsAtmosphereActive() && context.lightManager) {
                context.lightManager->SetAtmosphereSunTransmittance({ 1.0f, 1.0f, 1.0f });
            }
            context.atmosphereManager->ResetFrameActivation();
        }
        if (context.volumetricCloudManager) {
            context.volumetricCloudManager->ResetFrameActivation();
        }

        if (sceneManager) {
            sceneManager->FinalizeRenderFrame();
        }

#ifdef USE_IMGUI
        if (debug) debug->DrawImGuiWithProfiling(cmdList);

        // ゲーム映像専用ウィンドウへの転写。ImGui を描いた後に別のレンダーターゲットへ
        // 積むだけなので、メインバックバッファの内容には影響しない。
        if (debug) debug->RecordGameOutputWindow();

        if (debug) debug->EndRenderPipeline(cmdList, currentFrameIndex);
#endif // USE_IMGUI

        // フレームの最終処理（バックバッファ終了、コマンド実行、Present）
        // FinalizeFrame 内で SignalFrame されるフレームインデックスを事前に取得する
        const UINT currentFrameIndexForFlush =
            (dx && dx->GetCommandManager()) ? dx->GetCommandManager()->GetRecordingFrameIndex() : 0;

        if (render) {
            render->FinalizeFrame();
        }

#ifdef USE_IMGUI
        // 転写コマンドの実行が済んだこの位置で専用ウィンドウを Present する
        if (debug) debug->PresentGameOutputWindow();
#endif // USE_IMGUI

        // GPU 実行完了を確認してから DXR 退避リソースを解放
        if (auto* asMgr = context.accelerationStructureManager) {
            auto* commandManager = dx ? dx->GetCommandManager() : nullptr;
            asMgr->FlushRetiredResources(commandManager, currentFrameIndexForFlush);
        }

#ifdef USE_IMGUI
        if (debug) debug->PostFinalizeFrame(dx);
#endif // USE_IMGUI

        // frameViews はこの関数のローカル。フレーム外から参照されないよう参照を切る。
        if (renderManager) {
            renderManager->SetFrameViews(nullptr);
        }
    }

    // ──────────────────────────────────────────────────────────
    // コンポーネント作成ヘルパーメソッド
    // ──────────────────────────────────────────────────────────

#pragma region コンポーネントヘルパーメソッド

    void EngineSystem::CreateFrameRateController()
    {
        CoreComponentFactory::SetupFrameRate(*this);
    }

    void EngineSystem::CreateGraphicsComponents(const EngineConfig& config)
    {
        GraphicsComponentFactory::Setup(*this, config);
    }

    void EngineSystem::CreateInputComponents()
    {
        CoreComponentFactory::SetupInput(*this);
    }

    void EngineSystem::CreateAudioComponents()
    {
        CoreComponentFactory::SetupAudio(*this);
    }

    void EngineSystem::CreateLightComponents()
    {
        CoreComponentFactory::SetupLight(*this);
    }

    void EngineSystem::BuildDefaultRenderPipeline()
    {
        // レンダーパイプラインの作成
        // 各パスは (フェーズ, フェーズ内優先度) で登録する。
        // 実行順・バリアは各パスの DeclareResources 宣言から RenderGraph が導出する。
        renderPipeline_ = std::make_unique<RenderPipeline>();

        // フレーム前処理: DXR 加速構造構築（frameNumber ガードでフレーム内 1 回のみ実行）
        renderPipeline_->AddPass(std::make_unique<ASBuildPass>(), RenderPassPhase::FrameSetup);

        // フレーム前処理: 大気散乱 LUT 生成（パラメータ変更時のみ Compute 実行）
        // パス自身が SRV ヒープをバインドするため、フレーム先頭でも安全に実行できる
        renderPipeline_->AddPass(std::make_unique<AtmosphereLUTPass>(), RenderPassPhase::FrameSetup, 10);

        // フレーム前処理: ボリューメトリック雲のノイズ生成（ダーティ時のみ Compute 実行）
        renderPipeline_->AddPass(std::make_unique<VolumetricCloudNoisePass>(), RenderPassPhase::FrameSetup, 20);

        // G-Buffer 蓄積（不透明 Model / SkinnedModel の描画）
        renderPipeline_->AddPass(std::make_unique<GBufferPass>(), RenderPassPhase::GBuffer);

        // G-Buffer 完成直後: Hi-Z ピラミッド構築 + 遮蔽判定（メイン GameView のみ。
        // 結果はフレームリング一巡後の Model::Draw が Submit スキップに使う）
        renderPipeline_->AddPass(std::make_unique<HiZOcclusionPass>(hiZOcclusionSystem_.get()), RenderPassPhase::PreLighting, 5);

        // ライティング前処理: SSAO / RT シャドウ / コースティクス
        auto ssaoPass = std::make_unique<SSAOPass>();
        ssaoPass->SetSSAOTargetName(RenderTargetNames::SSAOBuffer);
        ssaoPass->SetSSAOBlurTargetName(RenderTargetNames::SSAOBlurBuffer);
        renderPipeline_->AddPass(std::move(ssaoPass), RenderPassPhase::PreLighting, 0);
        renderPipeline_->AddPass(std::make_unique<RTShadowPass>(), RenderPassPhase::PreLighting, 10);
        renderPipeline_->AddPass(std::make_unique<RTShadowTemporalPass>(), RenderPassPhase::PreLighting, 11);
        renderPipeline_->AddPass(std::make_unique<RTShadowDenoisePass>(), RenderPassPhase::PreLighting, 12);
        // コースティクスは実行順の都合で PreLighting だが、コストの分類は水面。
        // ここを Water へ寄せないと「水面がフレームに占める割合」から集光分が抜け落ちる。
        renderPipeline_->AddPass(
            std::make_unique<RTWaterCausticsPass>(), RenderPassPhase::PreLighting, 20,
            GpuTimingCategory::Water);
        renderPipeline_->AddPass(
            std::make_unique<WaterCausticsPass>(), RenderPassPhase::PreLighting, 30,
            GpuTimingCategory::Water);

        // Deferred ライティング: G-Buffer を読み取り SceneColor を生成
        renderPipeline_->AddPass(std::make_unique<DeferredLightingPass>(), RenderPassPhase::Lighting);

        // ライティング後: FFT 波面更新と空気遠近感の合成（GameView のみ）
        // 波面生成は実行順の都合で PostLighting に置いているが、コストの分類としては水面。
        // 計測カテゴリを Water へ寄せないと「水面がフレームに占める割合」を数え漏らす。
        renderPipeline_->AddPass(
            std::make_unique<FFTOceanPass>(), RenderPassPhase::PostLighting, 0,
            GpuTimingCategory::Water);
        renderPipeline_->AddPass(std::make_unique<AerialPerspectivePass>(), RenderPassPhase::PostLighting, 10);

        // Forward 合成（従来の大箱 GeometryPass をキュー単位の 3 パスへ分割）
        // 不透明 Model/SkinnedModel は投入時に GBuffer 経路へ振り分け済みなので含まれない
        renderPipeline_->AddPass(std::make_unique<GeometryPass>(), RenderPassPhase::Sky, 0);
        renderPipeline_->AddPass(std::make_unique<SkyBoxQueuePass>(), RenderPassPhase::Sky, 10);

        // ボリューメトリック雲: SkyBox 描画後の SceneColor へレイマーチ結果を合成（GameView のみ）
        renderPipeline_->AddPass(std::make_unique<VolumetricCloudPass>(), RenderPassPhase::Sky, 20);

        // ゴッドレイ: 雲シャドウマップ生成 + 内散乱の遮蔽差分を SceneColor へ合成（GameView のみ）
        renderPipeline_->AddPass(std::make_unique<GodRayPass>(), RenderPassPhase::Sky, 30);

        renderPipeline_->AddPass(std::make_unique<TransparentQueuePass>(), RenderPassPhase::Transparent, 0);

        // 水面: 背景 SceneColor の複製 → RT 屈折 → RT 反射 → 水面合成（データフロー順）
        renderPipeline_->AddPass(std::make_unique<SceneColorCopyPass>(), RenderPassPhase::Water, 0);
        renderPipeline_->AddPass(std::make_unique<RTWaterRefractionPass>(), RenderPassPhase::Water, 10);
        renderPipeline_->AddPass(std::make_unique<RTWaterReflectionPass>(), RenderPassPhase::Water, 15);
        renderPipeline_->AddPass(std::make_unique<WaterSurfacePass>(), RenderPassPhase::Water, 20);

        // TAA: トーンマップ前の HDR 空間で解決する（ポストエフェクト列より必ず前）
        renderPipeline_->AddPass(std::make_unique<TAAPass>(), RenderPassPhase::PostProcess, 0);

        // CAS: TAA でぼけた分のシャープ化。必ず TAA の直後
        renderPipeline_->AddPass(std::make_unique<CASPass>(), RenderPassPhase::PostProcess, 5);

        // ポストエフェクト（有効エフェクト列は Graph 構築時にノード分解される）
        renderPipeline_->AddPass(std::make_unique<PostEffectPass>(), RenderPassPhase::PostProcess, 10);

        // バックバッファへの最終出力
        auto backBufferPass = std::make_unique<BackBufferPass>();
        backBufferPass->SetRenderTargetName(RenderTargetNames::BackBuffer);  // 名前ベースで指定
        renderPipeline_->AddPass(std::move(backBufferPass), RenderPassPhase::Final);
    }

#pragma endregion
}
