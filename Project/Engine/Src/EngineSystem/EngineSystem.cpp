#include "pch.h"
#include "EngineSystem.h"
#include "Subsystem/RayTracingSubsystem.h"
#ifdef USE_IMGUI
#include "Settings/EditorSettingsSubsystem.h"
#endif
// ImGui 無しビルドでも CVar の保存値を適用するために常に必要
#include "Settings/CVarSettingsSection.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Factory/GraphicsComponentFactory.h"
#include "Factory/CoreComponentFactory.h"
#include "Startup/StartupSequence.h"
#include "Graphics/Shader/Cache/ShaderCacheStore.h"
#include "Graphics/Shader/Cache/ShaderManifest.h"
#include "Graphics/Shader/ShaderPrewarm.h"
#include <cstring>

// ユーティリティ
#include "Utility/Random/RandomGenerator.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Asset/AssetDatabase.h"

// EngineSystem が直接使う型
// （EngineSystem.h はサービス型ヘッダを配らないので、使う型は自分で include する）
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Audio/AudioSystem.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Utility/FrameRate/Time.h"

#if defined(USE_IMGUI) && defined(USE_PIX)
#include "Editor/ImGui/PixCapture.h"
#endif

// レンダーパイプライン
// 具象パス型（ASBuildPass / GBufferPass / …）を知るのは
// DefaultRenderPipelineBuilder.cpp だけ。ここへ include を戻さないこと
//（EngineSystem.h を include する全ファイルの再コンパイル対象になる）
#include "Graphics/Render/Pass/RenderPipeline.h"
#include "Graphics/Render/Pass/DefaultRenderPipelineBuilder.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Scene/IScene.h"

// Hi-Z オクルージョンカリング
#include "Graphics/Render/Culling/HiZOcclusionSystem.h"

// レイトレーシング
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"

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

    void EngineSystem::BuildStartupTasks(
        StartupSequence& sequence,
        WinApp* winApp,
        const EngineConfig& config,
        const std::function<void(StartupSequence&)>& buildPreloadTasks)
    {
        sequence.Add("基盤: COM / ログ / アセットデータベース", [this, winApp, config] {
            // COMの初期化
            CoInitializeEx(0, COINIT_MULTITHREADED);

            // ログシステムの初期化（最初に実行）
            // ここが済むまで StartupSequence もログを出せないので、
            // このステップは必ず先頭に置くこと
            Logger::GetInstance().Initialize();

            // WinAppのインスタンスを保持
            winApp_ = winApp;

            // アセットデータベースの初期化（テクスチャ読み込みより先に必要）
            AssetDatabase::GetInstance().Initialize(std::filesystem::current_path());

            // コンパイル済み DXIL のディスクキャッシュ。
            // 最初のシェーダコンパイル（レンダードメインのステップ）より前に
            // 用意しておく必要がある
            ShaderCacheStore::GetInstance().Initialize(
                std::filesystem::current_path() / "Cache" / "ShaderCache",
                config.enableShaderCache);

            // 「実際にコンパイルされるシェーダ」の一覧。次回の起動で並列に
            // 事前コンパイルするために使う。DXIL キャッシュとは別の場所に置く
            //（キャッシュを消して再コンパイルさせる操作で一覧まで消えると、
            //  一番効いてほしい場面で事前コンパイルが効かなくなる）
            ShaderManifest::GetInstance().Initialize(
                std::filesystem::current_path() / "Cache" / "ShaderManifest.txt",
                config.enableShaderCache);
        });

        // フレームレート制御（最初に初期化）
        sequence.Add("フレームレート制御", [this] { CreateFrameRateController(); });

        // オーディオはグラフィックスに一切依存しないので、ここで裏の初期化を始めてしまう。
        // 以降のシェーダコンパイル数秒の裏に隠れる（このステップ自体は即座に戻る）
        sequence.Add("オーディオ（非同期開始）", [this] { CreateAudioComponents(); });

#if defined(USE_IMGUI) && defined(USE_PIX)
        // PIX GPU キャプチャ DLL をロード（D3D12 デバイス作成より前に必要）
        // DLL がロードされると全 D3D12 API がフックされ ~33% のオーバーヘッドが発生するため、
        // コンフィグで明示的に有効化された場合のみロードする
        if (config.enablePixRuntime) {
            sequence.Add("PIX ランタイム", [] { PixCapture::LoadPixRuntime(); });
        }
#endif

        // グラフィックス関連（起動時間の大半。ファクトリ側でさらに細かく割る）。
        // 「デバイス + アセット土台 → シェーダ事前コンパイル → ゲームの先読み
        //   → レンダラー群」の順に並べる。
        auto graphicsState = GraphicsComponentFactory::BuildFoundationTasks(sequence, *this, config);

        // シェーダを全部まとめて並列にコンパイルし、DXIL を用意しておく。
        // 以降のレンダラー群（PSO 生成 20 箇所以上）はキャッシュヒットで済む。
        // モデル先読みより前に置くのは、並べると両方が CPU を食い合って
        // どちらのワーカーも半分の幅しか使えなくなるため（意図的に直列の phase へ分けた）。
        sequence.Add("シェーダ事前コンパイル（並列）", [] {
            ShaderPrewarm::Run();
        });

        if (buildPreloadTasks) {
            buildPreloadTasks(sequence);
        }
        GraphicsComponentFactory::BuildRendererTasks(sequence, *this, std::move(graphicsState));

        sequence.Add("入力", [this] { CreateInputComponents(); });

        // ライト関連（GraphicsComponents 後に初期化）
        sequence.Add("ライト", [this] { CreateLightComponents(); });

        sequence.Add("乱数生成器", [this] { RandomGenerator::GetInstance().Initialize(); });

        // ──────────────────────────────────────────────────────────
        // サブシステム登録 + 1 個ずつ初期化
        // ──────────────────────────────────────────────────────────
        // 「全部生成してから初期化」の順序は崩さないこと。
        // 初期化時点で全サブシステムが生成済みである前提のコードがある。
        sequence.Add("サブシステム生成", [this] {
            RegisterSubsystem<RayTracingSubsystem>();
#ifdef USE_IMGUI
            // エディタ設定の自動保存（セクション登録元より先に生成しておく）
            RegisterSubsystem<EditorSettingsSubsystem>();
            RegisterSubsystem<DebugSubsystem>();
#endif // USE_IMGUI
        });

        // 生成ステップが積む個数は静的に決まるので、インデックス指定で
        // 1 サブシステム 1 ステップに切り出せる。
        // （実行中にステップを追加すると StartupSequence の内部 vector が
        //   再確保され、実行中エントリの参照が壊れるので絶対にやらない）
#ifdef USE_IMGUI
        constexpr size_t kSubsystemCount = 3;
#else
        constexpr size_t kSubsystemCount = 1;
#endif
        for (size_t i = 0; i < kSubsystemCount; ++i) {
            sequence.Add(
                // 表示名は実行直前に問い合わせられるので、生成ステップ後なら実名が出る
                [this, i]() -> std::string {
                    return std::string("サブシステム: ")
                        + (i < subsystems_.size() ? subsystems_[i]->GetName() : "?");
                },
                [this, i, config] {
                    if (i < subsystems_.size()) {
                        subsystems_[i]->Initialize(this, config);
                    }
                });
        }

#ifndef USE_IMGUI
        // ImGui 無しビルドには EditorSettingsSubsystem が無く、セクション登録時の
        // CVar 復元経路ごと落ちるため、そのままだと全 CVar がコード既定値になる。
        // 較正済みのプロジェクト設定はゲームの見た目そのものなので、保存はせず
        // 読み込みだけをここで行う（DebugSubsystem::Initialize と同じ順番＝
        // サブシステム初期化直後に適用され、Debug ビルドと同じ結果になる）
        sequence.Add("CVar プロジェクト設定の適用", [] {
            CVarSettingsSection::LoadProjectConfigFile();
            // 静的初期化中（main より前）に溜まった CVar の警告をログへ流す
            CVarRegistry::Get().FlushPendingWarnings();
            CVarSettingsSection::LogOverriddenCVars();
        });
#endif // !USE_IMGUI

        sequence.Add("GameObject へのエンジン参照", [this] { GameObject::SetEngine(this); });

        // デフォルトレンダーパイプラインの構築
        sequence.Add("レンダーパイプライン構築", [this] { BuildDefaultRenderPipeline(); });
    }

    void EngineSystem::Finalize()
    {
        // サブシステムを登録の逆順で終了処理
        for (auto it = subsystems_.rbegin(); it != subsystems_.rend(); ++it) {
            (*it)->Finalize();
        }
        // 破棄する実体を指したままのインデックスを残さないよう、所有より先に引き剥がす
        subsystemIndex_.Clear();
        subsystems_.clear();

        // 起動時に仕掛けたモデル先読みがまだ走っている可能性があるので、
        // TextureManager / GraphicsCore を壊す前に必ず合流させる
        if (auto* modelManager = GetService<ModelManager>()) {
            modelManager->WaitForPreload();
        }

        // TextureManager
        TextureManager::GetInstance().Clear();

        // AssetDatabaseの終了処理
        AssetDatabase::GetInstance().Finalize();

        // Hi-Z オクルージョンカリングの GPU リソースを解放する
        // （GraphicsCore 破棄前に明示解放しないと LeakChecker の ReportLiveObjects に報告される。
        //   インスタンス自体は ~ModelVisibility の UnregisterTarget が空振りできるよう
        //   ここでは reset せず、EngineSystem のデストラクタまで生存させる）
        if (hiZOcclusionSystem_) {
            hiZOcclusionSystem_->Shutdown();
        }

        // RenderDomainContext を先にシャットダウンしてから GraphicsCore を解放する
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

        // オーディオの更新（フェードの進行と、鳴り終わった再生スロットの回収）。
        // ポーズ中もフェードは進めたいので UnscaledDeltaTime を渡す
        if (auto* audioSystem = GetService<AudioSystem>()) {
            audioSystem->Update(Time::UnscaledDeltaTime());
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

        auto* dx = GetService<GraphicsCore>();
        auto* renderManager = GetService<RenderManager>();
        auto* render = GetService<Render>();
        auto* sceneManager = GetService<SceneManager>();

        // ===== フレーム開始 =====
        // フレーム番号・記録先コマンドリスト・前フレームの後始末をここで確定し、
        // 以降は RenderContext 経由で配る
        // 各パス／レンダラーが dxCommon->GetCommandList() を呼ぶと供給点がその数だけ増え、
        // コマンドリストを複数化したときに全箇所を直す羽目になる。
        const FrameContext frame = dx ? dx->BeginFrame() : FrameContext{};
        ID3D12GraphicsCommandList* cmdList = frame.cmdList;

        // レンダリングコンテキストの構築
        FrameBlackboard frameBlackboard;
        RenderContext context;
        context.dxCommon = dx;
        context.cmdList = cmdList;
        context.renderManager = renderManager;
        context.rayTracingSubsystem = rayTracing;
        context.sceneManager = sceneManager;
        context.postEffectManager = GetService<PostEffectManager>();
        context.renderingTechniqueManager = GetService<RenderingTechniqueManager>();
        context.lightManager = GetService<LightManager>();
        context.frameBlackboard = &frameBlackboard;
        context.modelManager = GetService<ModelManager>();

        // ドメイン固有マネージャ（GBuffer / DXR / 水面 / 大気 / 雲 / 深度）は
        // 所有者である RenderDomainContext 自身に注入させる。
        // ここへ個別の代入を書き戻すと、ドメインマネージャを 1 つ増やすたびに
        // EngineSystem の編集が必要になる
        if (renderDomainContext_) {
            renderDomainContext_->PopulateRenderContext(context);
        }

        // フレーム番号は FrameSync が単一ソース（EngineSystem 側で別に数えない）
        context.frameNumber = frame.frameNumber;
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

        // ポストエフェクトへ今フレームの文脈を配る。
        // ビュー確定後・View ループ前のここが唯一の呼び出し点（View ごとに呼ぶと
        // 補助ビューの行列でエフェクトの状態が上書きされる）。
        if (context.postEffectManager) {
            PostEffectFrameContext postEffectContext;
            postEffectContext.view = &frameViews.GameView();
            postEffectContext.deltaTime = Time::DeltaTime();
            if (context.atmosphereManager) {
                postEffectContext.sunDirection = context.atmosphereManager->GetSunDirection();
                postEffectContext.sunDirectionValid = true;
            }
            context.postEffectManager->PrepareFrame(postEffectContext);
        }

        if (context.sceneDepth) {
            frameBlackboard.SetResource(
                FrameBlackboard::SceneDepth,
                context.sceneDepth->GetDepthSRVHandle(),
                &context.sceneDepth->Resource());
        }

#ifdef USE_IMGUI
        // プロファイラのリングスロットは今フレームのスロット番号に合わせる
        const UINT currentFrameIndex = frame.frameIndex;
        if (debug) debug->BeginRenderPipeline(cmdList, currentFrameIndex);
#endif

        // Hi-Z オクルージョンカリング: 完了済みリングスロットの可視性 Readback を反映する。
        // AABB 収集と遮蔽スキップの適用はメイン GameView の構築中のみ有効化する
        // （補助ビュー・反射ビューはカメラが異なり、メインカメラ基準の判定は誤カリングになる）。
        HiZOcclusionSystem* hiZOcclusion = hiZOcclusionSystem_.get();
        assert(hiZOcclusion && "HiZOcclusionSystem must be created by GraphicsComponentFactory");
        hiZOcclusion->BeginFrame(frame.frameIndex);
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

        // 全 View の描画（AerialPerspective 合成を含む）が完了したので、
        // ドメインマネージャのフレーム状態を後始末させる。
        // 個別機能（大気・雲）の都合はマネージャ側が持つ。ここへ機能名の分岐を書き戻さないこと
        if (renderDomainContext_) {
            renderDomainContext_->EndFrame(context.lightManager);
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

        // ===== フレーム終了 =====
        // バックバッファを PRESENT へ戻し、Close / Execute / Signal / Present / 次フレーム準備を行う
        if (render) {
            render->FinalizeFrame();
        }
        if (dx) {
            dx->EndFrame();
        }

#ifdef USE_IMGUI
        // 転写コマンドの実行が済んだこの位置で専用ウィンドウを Present する
        if (debug) debug->PresentGameOutputWindow();
#endif // USE_IMGUI

        // DXR の退避リソースを遅延解放キューへ預ける
        // （EndFrame() で今フレームを Signal した後に呼ぶこと。前だとフェンス値がずれる）
        if (auto* asMgr = context.accelerationStructureManager; asMgr && dx) {
            asMgr->MoveRetiredResourcesTo(dx->DeferredRelease(), dx->Frame().LastSignaledValue());
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
        // パイプラインの所有は EngineSystem、構成の中身は Pass モジュール側の責務。
        // 標準パス列を足す・並べ替える編集は DefaultRenderPipelineBuilder.cpp で完結し、
        // ここは変更されない。
        renderPipeline_ = std::make_unique<RenderPipeline>();
        DefaultRenderPipelineBuilder::Build(*renderPipeline_, hiZOcclusionSystem_.get());
    }

#pragma endregion
}
