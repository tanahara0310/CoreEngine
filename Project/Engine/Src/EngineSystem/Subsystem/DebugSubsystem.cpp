#include "pch.h"
#include "DebugSubsystem.h"

#ifdef USE_IMGUI

#include "../EngineSystem.h"
#include "EngineProfileScope.h"
#include "../EngineConfig.h"

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
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Debug/EngineStats.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Input/InputManager.h"
#include "Scene/SceneManager.h"
#include "ObjectCommon/GameObjectManager.h"
#include "ObjectCommon/Model/ModelGameObject.h"
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

        // エンジン統計ウィンドウ（EngineDebug メニュー：カテゴリ別個別ウィンドウ）
        engineStatsWindow_ = std::make_unique<EngineStatsWindow>();
        engineStatsWindow_->SetEngineSystem(engine_);
        engineStatsWindow_->SetGpuProfiler(&gpuProfiler_);
        if (auto* mm = engine_->GetComponent<ModelManager>()) {
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

        // Lighting パネル
        gameDebugUI_->RegisterEnginePanel("Lighting", [this]() {
            if (auto* lightManager = engine_->GetComponent<LightManager>()) {
                lightManager->DrawAllImGui();
            }
            });

        // Shading パネル
        gameDebugUI_->RegisterEnginePanel("Shading", [this]() {
            auto* sceneManager = engine_->GetSceneManager();
            auto* objManager = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;

            ImGui::SeparatorText("シーン全体に適用");

            static const char* kModeItems[] = {
                "PBR  (IBL なし)",
                "PBR + IBL",
                "Lambert  (従来)",
                "Half-Lambert  (従来)"
            };
            static int sceneWideShadingMode = 0;
            ImGui::SetNextItemWidth(220.0f);
            ImGui::Combo("モード##SceneWide", &sceneWideShadingMode, kModeItems, 4);

            ImGui::BeginDisabled(objManager == nullptr);
            if (ImGui::Button("シーン全体に適用", ImVec2(-1.0f, 0.0f))) {
                const auto mode = static_cast<ShadingMode>(sceneWideShadingMode);
                for (auto& obj : objManager->GetAllObjects()) {
                    auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get());
                    if (!modelObj) continue;
                    auto* model = modelObj->GetModel();
                    if (!model) continue;
                    auto* mat = model->GetMaterial();
                    if (mat) mat->SetShadingMode(mode);
                }
            }
            ImGui::EndDisabled();
            if (objManager == nullptr) {
                ImGui::TextDisabled("(シーンが存在しません)");
            }

            ImGui::Spacing();
            ImGui::SeparatorText("モデル別シェーディングモード");

            if (objManager) {
                int modelIndex = 0;
                for (auto& obj : objManager->GetAllObjects()) {
                    auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get());
                    if (!modelObj) continue;
                    auto* model = modelObj->GetModel();
                    if (!model) continue;
                    auto* mat = model->GetMaterial();
                    if (!mat) continue;

                    ImGui::PushID(modelIndex++);
                    const char* name = modelObj->GetObjectName();
                    ImGui::SetNextItemWidth(170.0f);
                    int mode = static_cast<int>(mat->GetShadingMode());
                    if (ImGui::Combo(name, &mode, kModeItems, 4)) {
                        mat->SetShadingMode(static_cast<ShadingMode>(mode));
                    }
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("(シーンが存在しません)");
            }
            });

        // Post Effects パネル（デフォルト表示）
        gameDebugUI_->RegisterEnginePanel("Post Effects", [this]() {
            if (auto* postEffect = engine_->GetComponent<PostEffectManager>()) {
                postEffect->DrawImGuiContent();
            }
            });
        gameDebugUI_->SetPanelVisible("Post Effects", true);

        // Rendering Techniques パネル（SSAO, TAA等のレンダリング技術）
        gameDebugUI_->RegisterEnginePanel("Rendering Techniques", [this]() {
            if (auto* renderingTechniqueManager = engine_->GetComponent<RenderingTechniqueManager>()) {
                renderingTechniqueManager->DrawImGui();
            }
            });

        // Render Pass デバッグパネル（各パスの中間バッファを可視化）
        {
            auto* renderDx = engine_->GetComponent<DirectXCommon>();
            auto* renderComp = engine_->GetComponent<Render>();
            renderPassDebugPanel_.Initialize(renderDx);
            renderPassDebugPanel_.SetRenderDomainContext(engine_->GetRenderDomainContext());
            if (renderComp) {
                renderPassDebugPanel_.SetRenderTargetManager(renderComp->GetRenderTargetManager());
            }
            gameDebugUI_->RegisterEnginePanel("Render Pass", [this]() {
                renderPassDebugPanel_.Draw();
                });
        }

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

        // フレーム開始時にレンダリング統計をリセット
        EngineStats::GetInstance().BeginFrame();

        // ImGuiの開始（PostEffectManagerとGameDebugUIを渡す）
        if (auto* postEffect = engine_->GetComponent<PostEffectManager>()) {
            imGui_->Begin(postEffect, gameDebugUI_.get());
        }

        //メニューバーを最初に描画（ドッキングスペースより前）
        gameDebugUI_->ShowMainMenuBar();

        // その他のデバッグUIの更新（メニューバー以外）
        gameDebugUI_->UpdateDebugPanels();

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
            auto* dx = engine_->GetComponent<DirectXCommon>();
            auto* postEffect = engine_->GetComponent<PostEffectManager>();
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

}

#endif // USE_IMGUI
