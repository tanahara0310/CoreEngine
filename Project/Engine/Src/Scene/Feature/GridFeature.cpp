#include "pch.h"

#ifdef USE_IMGUI

#include "GridFeature.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Editor/ImGui/DockingUI.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Render/Line/GridRenderer.h"

namespace CoreEngine
{
    void GridFeature::Initialize(SceneContext& ctx)
    {
        // GridRendererを作成
        gridRenderer_ = ctx.gameObjectManager->AddObject(std::make_unique<GridRenderer>());
        gridRenderer_->Initialize();

        // デフォルト設定
        gridRenderer_->SetGridSize(100.0f);
        gridRenderer_->SetSpacing(1.0f);
        gridRenderer_->SetVisible(true);

        // グリッドはシーンデータに保存しない
        gridRenderer_->SetSerializeEnabled(false);
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

    void GridFeature::Finalize(SceneContext&)
    {
        // 所有権は GameObjectManager にあるためポインタのみクリア
        gridRenderer_ = nullptr;
    }
}

#endif // USE_IMGUI
