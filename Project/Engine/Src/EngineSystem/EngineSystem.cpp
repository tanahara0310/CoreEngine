#include "pch.h"
#include "EngineSystem.h"
#include "Subsystem/EngineProfileScope.h"
#include "Subsystem/RayTracingSubsystem.h"
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
#include "Utility/Debug/ImGui/PixCapture.h"
#endif

// レンダーパス
#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Graphics/Render/Pass/ShadowMapPass.h"
#include "Graphics/Render/Pass/GBufferPass.h"
#include "Graphics/Render/Pass/SSAOPass.h"
#include "Graphics/Render/Pass/DeferredLightingPass.h"
#include "Graphics/Render/Pass/RTShadowPass.h"
#include "Graphics/Render/Pass/RTWaterCausticsPass.h"
#include "Graphics/Render/Pass/RTWaterRefractionPass.h"
#include "Graphics/Render/Pass/FFTOceanPass.h"
#include "Graphics/Render/Pass/AtmosphereLUTPass.h"
#include "Graphics/Render/Pass/AerialPerspectivePass.h"
#include "Graphics/Render/Pass/WaterCausticsPass.h"
#include "Graphics/Render/Pass/GeometryPass.h"
#include "Graphics/Render/Pass/SceneColorCopyPass.h"
#include "Graphics/Render/Pass/WaterSurfacePass.h"
#include "Graphics/Render/Pass/PostEffectPass.h"
#include "Graphics/Render/Pass/BackBufferPass.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Scene/IScene.h"

// レイトレーシング
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"

#include "ObjectCommon/GameObject.h"
#include "Scene/SceneManager.h"
#include "EngineSystem/EngineConfig.h"


namespace CoreEngine
{
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

        auto* dx = GetComponent<DirectXCommon>();
        auto* renderManager = GetComponent<RenderManager>();
        auto* render = GetComponent<Render>();
        auto* sceneManager = GetComponent<SceneManager>();

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
        context.postEffectManager = GetComponent<PostEffectManager>();
        context.renderingTechniqueManager = GetComponent<RenderingTechniqueManager>();
        context.lightManager = GetComponent<LightManager>();
        context.gBufferManager = renderDomainContext_ ? renderDomainContext_->GetGBufferManager() : nullptr;
        context.shadowMapManager = renderDomainContext_ ? renderDomainContext_->GetShadowMapManager() : nullptr;
        context.accelerationStructureManager = renderDomainContext_ ? renderDomainContext_->GetAccelerationStructureManager() : nullptr;
        context.rtShadowManager = renderDomainContext_ ? renderDomainContext_->GetRayTracingShadowManager() : nullptr;
        context.rtWaterCausticsManager = renderDomainContext_ ? renderDomainContext_->GetWaterCausticsRayTracingManager() : nullptr;
        context.rtWaterRefractionManager = renderDomainContext_ ? renderDomainContext_->GetWaterRefractionRayTracingManager() : nullptr;
        context.fftOceanManager = renderDomainContext_ ? renderDomainContext_->GetFFTOceanManager() : nullptr;
        context.atmosphereManager = renderDomainContext_ ? renderDomainContext_->GetAtmosphereManager() : nullptr;
        context.depthStencilManager = dx ? dx->GetDepthStencilManager() : nullptr;
        context.frameBlackboard = &frameBlackboard;
        context.waterRefractionSurfaceData = sceneManager ? sceneManager->GetWaterRefractionSurfaceData() : nullptr;

        if (dx) {
            frameBlackboard.SetResource(
                FrameBlackboard::SceneDepth,
                dx->GetDepthStencilSRV(),
                dx->GetDepthStencilResource(),
                context.depthStencilManager ? &context.depthStencilManager->GetCurrentState() : nullptr);
        }

        // RenderTargetManagerを設定
        if (render) {
            context.renderTargetManager = render->GetRenderTargetManager();
        }

        // コマンドリストは EngineProfileScope に渡すため USE_IMGUI 外で宣言する
        // （リリースビルドでは EngineProfileScope は no-op）
        ID3D12GraphicsCommandList* cmdList = dx ? dx->GetCommandList() : nullptr;

#ifdef USE_IMGUI
        const UINT currentFrameIndex = dx ? dx->GetSwapChain()->GetCurrentBackBufferIndex() : 0;
        if (debug) debug->BeginRenderPipeline(cmdList, currentFrameIndex);
#endif

        // DXR BLAS / TLAS 構築（RayTracingSubsystem に委譲）
        if (rayTracing) {
            rayTracing->BuildAccelerationStructures(
                context, dx, GetComponent<ModelManager>(), GetComponent<SceneManager>());
        }

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

        {
            // GameView の主要描画は ShadowMap を含む RenderGraph へ統一して実行する。
            EngineProfileScope scope(this, GpuTimestampSlot::GBufferPass, cmdList);
            renderPipeline_->ExecuteView(context);
        }

        if (sceneManager) {
            sceneManager->FinalizeRenderFrame();
        }

#ifdef USE_IMGUI
        if (debug) debug->DrawImGuiWithProfiling(cmdList);
        if (debug) debug->EndRenderPipeline(cmdList, currentFrameIndex);
#endif // USE_IMGUI

        // フレームの最終処理（バックバッファ終了、コマンド実行、Present）
        // FinalizeFrame 内で SignalFrame されるフレームインデックスを事前に取得する
        const UINT currentFrameIndexForFlush =
            (dx && dx->GetSwapChain()) ? dx->GetSwapChain()->GetCurrentBackBufferIndex() : 0;

        if (render) {
            render->FinalizeFrame();
        }

        // GPU 実行完了を確認してから DXR 退避リソースを解放
        if (auto* asMgr = context.accelerationStructureManager) {
            auto* commandManager = dx ? dx->GetCommandManager() : nullptr;
            asMgr->FlushRetiredResources(commandManager, currentFrameIndexForFlush);
        }

#ifdef USE_IMGUI
        if (debug) debug->PostFinalizeFrame(dx);
#endif // USE_IMGUI
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
        renderPipeline_ = std::make_unique<RenderPipeline>();

        // 1. シャドウマップパス
        auto shadowMapPass = std::make_unique<ShadowMapPass>();
        renderPipeline_->AddPass(std::move(shadowMapPass));

        // 1.5. 大気散乱 LUT 生成パス（パラメータ変更時のみ Compute 実行）
        auto atmosphereLUTPass = std::make_unique<AtmosphereLUTPass>();
        renderPipeline_->AddPass(std::move(atmosphereLUTPass));

        // 2. G-Bufferパス（不透明 Model / SkinnedModel の描画）
        auto gBufferPass = std::make_unique<GBufferPass>();
        renderPipeline_->AddPass(std::move(gBufferPass));

        // 2.5. SSAOパス（GBufferからワールド位置と法線を使用してAOを生成）
        auto ssaoPass = std::make_unique<SSAOPass>();
        ssaoPass->SetSSAOTargetName(RenderTargetNames::SSAOBuffer);
        ssaoPass->SetSSAOBlurTargetName(RenderTargetNames::SSAOBlurBuffer);
        renderPipeline_->AddPass(std::move(ssaoPass));

        // 2.75. RT シャドウパス（GameView のみ）
        // Step 2 段階では既存 RayTracingSubsystem のディスパッチ処理を薄くラップし、
        // GBuffer/SSAO 後、DeferredLighting 前に差し込む。
        auto rtShadowPass = std::make_unique<RTShadowPass>();
        renderPipeline_->AddPass(std::move(rtShadowPass));

        auto fftOceanPass = std::make_unique<FFTOceanPass>();
        renderPipeline_->AddPass(std::move(fftOceanPass));

        auto rtWaterCausticsPass = std::make_unique<RTWaterCausticsPass>();
        renderPipeline_->AddPass(std::move(rtWaterCausticsPass));

        auto rtWaterRefractionPass = std::make_unique<RTWaterRefractionPass>();
        renderPipeline_->AddPass(std::move(rtWaterRefractionPass));

        auto waterCausticsPass = std::make_unique<WaterCausticsPass>();
        renderPipeline_->AddPass(std::move(waterCausticsPass));

        // 3. DeferredLightingパス
        // G-Buffer (AlbedoAO / NormalRoughness / EmissiveMetallic) を読み取り、
        // 遅延ライティングを計算して現在の SceneColor ターゲットへ書き込む
        auto deferredLightingPass = std::make_unique<DeferredLightingPass>();
        renderPipeline_->AddPass(std::move(deferredLightingPass));

        // 3.5. 空気遠近感パス（不透明ジオメトリへ大気の霞を合成。GameView のみ）
        auto aerialPerspectivePass = std::make_unique<AerialPerspectivePass>();
        renderPipeline_->AddPass(std::move(aerialPerspectivePass));

        // 4. ジオメトリパス（透過オブジェクト / SkyBox / UI / パーティクル 等の Forward 描画）
        // DeferredLightingPass が SceneColor ターゲットへ書き込んだ結果の上に重ね描きする
        // 不透明 Model/SkinnedModel は GBufferPass + DeferredLightingPass で処理済みなので描画しない
        auto geometryPass = std::make_unique<GeometryPass>();
        renderPipeline_->AddPass(std::move(geometryPass));

        auto sceneColorCopyPass = std::make_unique<SceneColorCopyPass>();
        renderPipeline_->AddPass(std::move(sceneColorCopyPass));

        auto waterSurfacePass = std::make_unique<WaterSurfacePass>();
        renderPipeline_->AddPass(std::move(waterSurfacePass));

        // 5. ポストエフェクトパス
        auto postEffectPass = std::make_unique<PostEffectPass>();
        renderPipeline_->AddPass(std::move(postEffectPass));

        // 6. バックバッファパス（最終出力）
        auto backBufferPass = std::make_unique<BackBufferPass>();
        backBufferPass->SetRenderTargetName(RenderTargetNames::BackBuffer);  // 名前ベースで指定
        renderPipeline_->AddPass(std::move(backBufferPass));
    }

#pragma endregion
}
