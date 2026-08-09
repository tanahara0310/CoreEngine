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
        // GridRenderer は GameObject ではなく ILineSource。
        // Feature が所有し、Line パスへ供給元として登録する（Hierarchy には出ない）。
        gridRenderer_ = std::make_unique<GridRenderer>();
        // 最細 1m 格子。カメラ高度 5m でこの段になり、以降は高度 10 倍ごとに 1 段粗くなる
        gridRenderer_->SetBaseSpacing(1.0f);
        gridRenderer_->SetHeightAtBaseSpacing(5.0f);
        gridRenderer_->SetVisible(true);

        if (auto* pipeline = GetLinePipeline(ctx)) {
            pipeline->RegisterLineSource(gridRenderer_.get());
        }

        // Engine Settings の「Grid」パネル（設定 UI）を向ける
        GridRenderer::EnsureSettingsPanelRegistered(ctx.engine);
        GridRenderer::SetActiveForSettingsPanel(gridRenderer_.get());
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
            pipeline->UnregisterLineSource(gridRenderer_.get());
        }
        gridRenderer_.reset();
    }
}

#endif // USE_IMGUI
