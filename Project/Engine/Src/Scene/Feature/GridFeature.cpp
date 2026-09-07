#include "pch.h"

#ifdef USE_IMGUI

#include "GridFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Editor/ImGui/DockingUI.h"
#include "Graphics/Render/Line/GridRenderer.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Render/RenderManager.h"

namespace CoreEngine
{
    namespace {
        /// Line パスのパイプラインを取得する（無ければ nullptr）
        LineRendererPipeline* GetLinePipeline(SceneContext& ctx)
        {
            auto* renderManager = ctx.engine ? ctx.engine->GetService<RenderManager>() : nullptr;
            if (!renderManager) { return nullptr; }
            return static_cast<LineRendererPipeline*>(
                renderManager->GetRenderer(RenderPassType::Line));
        }
    }

    GridFeature::GridFeature() = default;
    GridFeature::~GridFeature() = default;

    void GridFeature::Initialize(SceneContext& ctx)
    {
        // GridRenderer の実体は RenderManager が Grid パスとして持っている（GameObject ではない）。
        // ここでは表示を有効にし、Y 軸ラインの供給元として Line パスへ登録するだけ。
        auto* renderManager = ctx.engine ? ctx.engine->GetService<RenderManager>() : nullptr;
        gridRenderer_ = renderManager
            ? static_cast<GridRenderer*>(renderManager->GetRenderer(RenderPassType::Grid))
            : nullptr;
        if (!gridRenderer_) {
            return;
        }

        // 最細 1m 格子。ここから先は「1 マスが画面上で潰れる手前で 10 倍粗い段へ」自動で切り替わる
        gridRenderer_->SetBaseSpacing(1.0f);
        gridRenderer_->SetMinPixelsPerCell(20.0f);
        gridRenderer_->SetVisible(true);

        // 垂直な Y 軸だけは床平面に乗らないので、従来どおり Line パスから描く
        if (auto* pipeline = GetLinePipeline(ctx)) {
            pipeline->RegisterLineSource(gridRenderer_);
        }

        // Engine Settings の「Grid」パネル（設定 UI）を向ける
        GridRenderer::EnsureSettingsPanelRegistered(ctx.engine);
        GridRenderer::SetActiveForSettingsPanel(gridRenderer_);
    }

    void GridFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart) {
            return;
        }

        // グリッド表示状態を更新
        if (gridRenderer_) {
            if (auto* debug = ctx.engine->GetDebugSubsystem()) {
                if (auto* dockingUI = debug->GetDockingUI()) {
                    gridRenderer_->SetVisible(dockingUI->IsGridVisible());
                }
            }
        }
    }

    void GridFeature::Finalize(SceneContext& ctx)
    {
        // シーンと一緒に消えるので、パイプラインの登録とパネルの参照を先に外す
        GridRenderer::SetActiveForSettingsPanel(nullptr);
        if (auto* pipeline = GetLinePipeline(ctx)) {
            pipeline->UnregisterLineSource(gridRenderer_);
        }
        // 実体は RenderManager のものなので破棄しない。シーンと一緒に消えるのは表示だけ
        if (gridRenderer_) {
            gridRenderer_->SetVisible(false);
        }
        gridRenderer_ = nullptr;
    }
}

#endif // USE_IMGUI
