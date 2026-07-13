#include "pch.h"

#ifdef USE_IMGUI

#include "DebugEditorFeature.h"
#include "Editor/Scene/SceneDebugEditor.h"

namespace CoreEngine
{
    DebugEditorFeature::DebugEditorFeature() = default;
    DebugEditorFeature::~DebugEditorFeature() = default;

    void DebugEditorFeature::Initialize(SceneContext& ctx)
    {
        debugEditor_ = std::make_unique<SceneDebugEditor>();
        debugEditor_->Initialize(ctx.engine, ctx.gameObjectManager,
            ctx.cameraManager, ctx.saveSystem);
    }

    void DebugEditorFeature::Update(SceneContext&, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart) {
            return;
        }

        debugEditor_->Update();
    }

    void DebugEditorFeature::Finalize(SceneContext&)
    {
        // デバッグ編集履歴をクリア
        if (debugEditor_) {
            debugEditor_->ClearHistory();
        }
    }
}

#endif // USE_IMGUI
