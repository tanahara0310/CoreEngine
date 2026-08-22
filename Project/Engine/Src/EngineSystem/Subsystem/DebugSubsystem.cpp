#include "pch.h"
#include "DebugSubsystem.h"

#ifdef USE_IMGUI

#include "../EngineSystem.h"
#include "EngineProfileScope.h"
#include "../EngineConfig.h"
#include "../Settings/EditorSettingsSubsystem.h"
#include "Editor/ImGui/EditorSettingsPanel.h"
#include "Utility/CVar/CVarRegistry.h"

#include "WinApp/WinApp.h"
#include "Utility/Logger/Logger.h"
#include "Threading/ThreadPool.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/Pass/GBufferPass.h"
#include "Graphics/Render/Pass/DeferredLightingPass.h"
#include "Graphics/Render/Pass/GeometryPass.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Diagnostics/EngineStats.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Input/InputManager.h"
#include "Scene/SceneManager.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/GameObjectManager.h"
#include <imgui.h>

namespace CoreEngine
{
    DebugSubsystem::DebugSubsystem()
        : imGui_(std::make_unique<ImGuiManager>())
        , gameDebugUI_(std::make_unique<GameDebugUI>())
    {
    }

    DebugSubsystem::~DebugSubsystem() = default;

    void DebugSubsystem::Initialize(EngineSystem* engine, [[maybe_unused]] const EngineConfig& config)
    {
        engine_ = engine;

        auto* dx = engine_->GetService<GraphicsCore>();

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
            if (auto* mm = engine_->GetService<ModelManager>()) { return mm->GetThreadPool(); }
            return nullptr;
            });
        gameDebugUI_->RegisterEnginePanel("Thread Profiler", [this]() {
            threadProfilerUI_->Draw();
            }, EnginePanelCategory::Tools, EnginePanelGroup::Analysis);

        // キーコンフィグUIの登録
        gameDebugUI_->RegisterEnginePanel("Key Config", [this]() {
            if (auto* inputManager = engine_->GetService<InputManager>()) {
                keyConfigUI_.Draw(inputManager->GetQuery());
            }
            }, EnginePanelCategory::Tools, EnginePanelGroup::Editor);

        // エンジン統計ウィンドウ（EngineDebug メニュー：カテゴリ別個別ウィンドウ）
        engineStatsWindow_ = std::make_unique<EngineStatsWindow>();
        engineStatsWindow_->SetEngineSystem(engine_);
        engineStatsWindow_->SetGpuProfiler(&gpuProfiler_);
        if (auto* mm = engine_->GetService<ModelManager>()) {
            engineStatsWindow_->SetModelManager(mm);
        }

        // Collect() はフレームごとに1回だけ呼ぶ（描画後に DrawImGuiWithProfiling 直前で実行）
        gameDebugUI_->RegisterEngineEditor("パフォーマンス", [this]() {
            engineStatsWindow_->DrawPerformanceTab();
            });
        gameDebugUI_->RegisterEngineEditor("レンダリング", [this]() {
            engineStatsWindow_->DrawRenderingTab();
            });
        gameDebugUI_->RegisterEngineEditor("シーン", [this]() {
            engineStatsWindow_->DrawSceneTab();
            });
        gameDebugUI_->RegisterEngineEditor("リソース", [this]() {
            engineStatsWindow_->DrawResourceTab();
            });
        gameDebugUI_->RegisterEngineEditor("メモリ", [this]() {
            engineStatsWindow_->DrawMemoryTab();
            });

        // ── ドメイン固有パネルの登録 ──

        // Lighting は環境エディタとして Hierarchy の Environment ツリーから選択して編集する。
        // 配下に各ライトを子行として列挙し、選択したライトを Inspector に表示する（Unity 風）
        gameDebugUI_->RegisterEnvironmentEditor("Lighting", this,
            [this]() {
                if (auto* lightManager = engine_->GetService<LightManager>()) {
                    lightManager->DrawAllImGui();
                }
            },
            [this]() -> bool {
                if (auto* lightManager = engine_->GetService<LightManager>()) {
                    return lightManager->DrawLightTreeImGui();
                }
                return false;
            },
            [this]() {
                if (auto* lightManager = engine_->GetService<LightManager>()) {
                    lightManager->ClearLightUISelection();
                }
            });

        // 大気散乱・雲は全シーン既定の機能のため、シーン所有の facade ではなく
        // エンジン寿命で常時登録する（どのシーンでも Environment ツリーから編集できる）
        atmosphereEditor_ = std::make_unique<AtmosphereEditor>();
        atmosphereEditor_->Initialize(*engine_);
        cloudEditor_ = std::make_unique<VolumetricCloudEditor>();
        cloudEditor_->Initialize(*engine_);

        // エディタ設定の自動保存セクション（登録時に保存済み JSON から前回状態が復元される）。
        // CVar が増えてもここへの追記は不要（レジストリを走査するため）。
        // プロジェクト設定（r./sys. → Config/EngineSettings/CVars.json）と
        // 個人の作業状態（d. → Saved/EditorSettings/EditorState.json）の 2 セクションに分ける
        if (auto* editorSettings = engine_->GetSubsystem<EditorSettingsSubsystem>()) {
            // 旧形式（Saved/EditorSettings/CVars.json 全部入り）があれば先に 2 層へ移行する
            CVarSettingsSection::MigrateLegacyFile();

            cvarConfigSection_ = std::make_unique<CVarSettingsSection>(/*userStatePart=*/false);
            editorSettings->RegisterSection(cvarConfigSection_.get(), this);
            cvarStateSection_ = std::make_unique<CVarSettingsSection>(/*userStatePart=*/true);
            editorSettings->RegisterSection(cvarStateSection_.get(), this);
        }

        // 静的初期化中（main より前）に溜まった CVar の警告をログへ流す。
        // 登録数もここで出るので、想定より少なければ定義漏れに気づける
        CVarRegistry::Get().FlushPendingWarnings();

        // 保存値で既定値から上書きされている CVar の一覧（両セクションの復元後に 1 回）
        CVarSettingsSection::LogOverriddenCVars();

        // 全 CVar の一覧・検索パネル（機能別パネルとは別に、横断的に触るための入口）
        // Engine Settings ウィンドウの「Editor Settings」管理パネル
        // （自動保存セクションの一覧・最終保存時刻・リセット / バックアップ復元）
        gameDebugUI_->RegisterEnginePanel("Editor Settings", [this]() {
            EditorSettingsPanel::Draw(engine_ ? engine_->GetSubsystem<EditorSettingsSubsystem>() : nullptr);
        }, EnginePanelCategory::Settings, EnginePanelGroup::Editor);

        // Shading パネル（IBL はシーン側で有効化され、マテリアルは強度のみ持つ）
        gameDebugUI_->RegisterEnginePanel("Shading", [this]() {
            auto* sceneManager = engine_->GetSceneManager();
            auto* objManager = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;

            ImGui::SeparatorText("シーン全体に適用");

            static float sceneWideIBLIntensity = 1.0f;
            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderFloat("IBL 強度##SceneWide", &sceneWideIBLIntensity, 0.0f, 2.0f);

            ImGui::BeginDisabled(objManager == nullptr);
            if (ImGui::Button("シーン全体に適用", ImVec2(-1.0f, 0.0f))) {
                // メッシュを持つものだけを回る（具象クラスへのダウンキャストは不要）
                objManager->ForEachComponent<MeshRendererComponent>(
                    [](MeshRendererComponent& renderer) {
                        auto* model = renderer.GetModel();
                        if (!model) return;
                        model->ForEachMaterial([](MaterialInstance* mat) {
                            mat->SetIBLIntensity(sceneWideIBLIntensity);
                        });
                    });
            }
            ImGui::EndDisabled();
            if (objManager == nullptr) {
                ImGui::TextDisabled("(シーンが存在しません)");
            }

            ImGui::Spacing();
            ImGui::SeparatorText("モデル別 IBL 強度");

            if (objManager) {
                int modelIndex = 0;
                objManager->ForEachComponent<MeshRendererComponent>(
                    [&modelIndex](MeshRendererComponent& renderer, GameObject& owner) {
                        auto* model = renderer.GetModel();
                        if (!model) return;
                        auto* mat = model->GetMaterial();
                        if (!mat) return;

                        ImGui::PushID(modelIndex++);
                        const char* name = owner.GetObjectName();
                        ImGui::SetNextItemWidth(170.0f);
                        float intensity = mat->GetIBLIntensity();
                        if (ImGui::SliderFloat(name, &intensity, 0.0f, 2.0f)) {
                            // スロット0の値を代表値として全スロットへ反映する
                            model->ForEachMaterial([intensity](MaterialInstance* m) {
                                m->SetIBLIntensity(intensity);
                            });
                        }
                        ImGui::PopID();
                    });
            } else {
                ImGui::TextDisabled("(シーンが存在しません)");
            }
            }, EnginePanelCategory::Settings, EnginePanelGroup::Rendering);

        // Post Effects セクション（Engine Settings 内）
        gameDebugUI_->RegisterEnginePanel("Post Effects", [this]() {
            if (auto* postEffect = engine_->GetService<PostEffectManager>()) {
                postEffect->DrawImGuiContent();
            }
            }, EnginePanelCategory::Settings, EnginePanelGroup::Rendering);

        // Rendering Techniques パネル（SSAO, TAA等のレンダリング技術）
        gameDebugUI_->RegisterEnginePanel("Rendering Techniques", [this]() {
            if (auto* renderingTechniqueManager = engine_->GetService<RenderingTechniqueManager>()) {
                renderingTechniqueManager->DrawImGui();
            }
            }, EnginePanelCategory::Settings, EnginePanelGroup::Rendering);

        // Render Pass デバッグパネル（各パスの中間バッファを可視化）
        {
            auto* renderDx = engine_->GetService<GraphicsCore>();
            auto* renderComp = engine_->GetService<Render>();
            renderPassDebugPanel_.Initialize(renderDx);
            renderPassDebugPanel_.SetRenderDomainContext(engine_->GetRenderDomainContext());
            if (renderComp) {
                renderPassDebugPanel_.SetRenderTargetManager(renderComp->GetRenderTargetManager());
            }
            gameDebugUI_->RegisterEnginePanel("Render Pass", [this]() {
                renderPassDebugPanel_.Draw();
                }, EnginePanelCategory::Tools, EnginePanelGroup::Rendering);
        }

        // ゲーム映像だけを映す専用ウィンドウ（ImGui を経由しない自前の HWND＋スワップチェーン）
        gameOutputWindow_.Initialize(dx, engine_->GetService<PostEffectManager>(),
            engine_->GetWinApp() ? engine_->GetWinApp()->GetHwnd() : nullptr);

        // RenderGraph ノードエディタ（imnodes）。
        // パスの依存・実行順・GPU 時間・バリアを 1 枚のグラフとして見せ、
        // ノードから直接パスの有効/無効を切り替えられるようにする。
        renderGraphEditorPanel_.Initialize(engine_, &gpuProfiler_);
        gameDebugUI_->RegisterEnginePanel("Render Graph", [this]() {
            renderGraphEditorPanel_.Draw();
            }, EnginePanelCategory::Tools, EnginePanelGroup::Rendering);

        // レイトレーシング専用デバッグパネル（Debug メニュー > Ray Tracing）
        // 加速構造の統計・RTシャドウのステージ別内訳・中間バッファ・設定をまとめる。
        rayTracingDebugPanel_.Initialize(engine_, &gpuProfiler_);
        gameDebugUI_->RegisterEngineDebugPanel("Ray Tracing", [this]() {
            rayTracingDebugPanel_.Draw();
            }, EnginePanelGroup::Rendering);

        // その他の固定ウィンドウをドッキングシステムに登録
        DockingUI* dockingUI = imGui_->GetDockingUI();
        if (dockingUI) {
            // GameViewportが作成するウィンドウを中央に配置
            dockingUI->RegisterWindow("Game", DockArea::Center);

            // Canvasプレビューウィンドウを Game と同じ位置にタブとして配置
            dockingUI->RegisterWindow("Canvas", DockArea::Center);

            // パーティクルシステムデバッグを右側に配置
            dockingUI->RegisterWindow("Particle System Debug", DockArea::Right);
        }

    }

    void DebugSubsystem::Finalize()
    {
        // エディタ設定セクションの解除（解除時に最終保存が走る）。
        // EngineSystem::Finalize は登録の逆順で呼ぶため、この時点で
        // EditorSettingsSubsystem はまだ Finalize されていない
        if (engine_) {
            if (auto* editorSettings = engine_->GetSubsystem<EditorSettingsSubsystem>()) {
                editorSettings->UnregisterSections(this);
            }
        }
        cvarConfigSection_.reset();
        cvarStateSection_.reset();

        // コンソールUIへのログ転送を解除（ImGui解放前に行う）
        Logger::GetInstance().ClearConsoleCallback();

        // ゲーム映像専用ウィンドウ（GPU 待ちを含むので ImGui / プロファイラより先に畳む）
        gameOutputWindow_.Finalize();

        // RenderGraph エディタの終了処理（imnodes コンテキストは ImGui より先に解放する）。
        // パスの有効状態を上書きしたまま終わらないよう、ここで元へ戻す。
        renderGraphEditorPanel_.Finalize();

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

        // フレーム開始時にレンダリング統計をリセット
        EngineStats::GetInstance().BeginFrame();

        // RenderGraph エディタが閉じられていればスナップショット複製を止める
        //（Draw() はウィンドウが開いている間しか呼ばれないため、止める判断はここでしかできない）
        renderGraphEditorPanel_.SyncCaptureState();

        // ゲーム映像専用ウィンドウの生成・破棄・リサイズはここで確定させる。
        // GPU 待ちを伴うため、コマンドリストへの記録が始まる前でなければならない。
        if (gameDebugUI_) {
            if (gameOutputWindow_.ConsumeCloseRequest()) {
                gameDebugUI_->SetStandaloneGameWindowVisible(false);
            }
            gameOutputWindow_.RequestVisible(gameDebugUI_->IsStandaloneGameWindowVisible());
        }
        gameOutputWindow_.ApplyPendingRequests();

        // ImGuiの開始（PostEffectManagerとGameDebugUIを渡す）
        if (auto* postEffect = engine_->GetService<PostEffectManager>()) {
            imGui_->Begin(postEffect, gameDebugUI_.get());
        }

        // F11 でエディタUIを退避している間はメニューバーもパネルも出さない
        //（表示状態の判定は ImGuiManager::Begin がキー入力を処理した後に行うこと）
        if (imGui_->IsEditorUiVisible()) {
            //メニューバーを最初に描画（ドッキングスペースより前）
            gameDebugUI_->ShowMainMenuBar();

            // その他のデバッグUIの更新（メニューバー以外）
            gameDebugUI_->UpdateDebugPanels();
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
        // 全描画完了後に統計を収集（ドローコール数等が確定した後）
        if (engineStatsWindow_) {
            engineStatsWindow_->Collect();
        }

        EngineProfileScope scope(engine_, GpuTimestampSlot::ImGuiDraw, cmdList);
        if (imGui_) {
            // PostEffectPass完了後に最新の finalDisplayHandle_ でGameビューを描画
            auto* dx = engine_->GetService<GraphicsCore>();
            auto* postEffect = engine_->GetService<PostEffectManager>();
            imGui_->DrawGameViewport(dx, postEffect, gameDebugUI_.get());
            imGui_->Draw();
        }
    }

    void DebugSubsystem::EndRenderPipeline(ID3D12GraphicsCommandList* cmdList, UINT frameIndex)
    {
        gpuProfiler_.EndCpuTimestamp(GpuTimestampSlot::Total);
        gpuProfiler_.EndGpuTimestamp(GpuTimestampSlot::Total, cmdList);
        gpuProfiler_.ResolveAll(cmdList, frameIndex);
    }

    void DebugSubsystem::RecordGameOutputWindow()
    {
        gameOutputWindow_.RecordDrawCommands();
    }

    void DebugSubsystem::PresentGameOutputWindow()
    {
        gameOutputWindow_.Present();
    }

    void DebugSubsystem::PostFinalizeFrame(GraphicsCore* dx)
    {
        if (!dx) {
            return;
        }
        // EndFrame でローテーション済みの次フレームスロットを使う
        // （スワップチェーンのインデックスはリサイズで 0 にリセットされるため使わない）
        const UINT nextFrameIndex = dx->Frame().FrameIndex();
        gpuProfiler_.ReadResults(dx->GetCommandQueue(), nextFrameIndex);

        if (auto* dockingUI = GetDockingUI()) {
            dockingUI->SetTimingData(gpuProfiler_.GetResults());
        }

        // メインウィンドウの Present が済んだこの位置で、外へ出された ImGui ウィンドウを描く。
        // エンジンのコマンドリスト記録中（DrawImGuiWithProfiling など）では呼べない。
        if (imGui_) {
            imGui_->RenderPlatformWindows();
        }
    }

}

#endif // USE_IMGUI
