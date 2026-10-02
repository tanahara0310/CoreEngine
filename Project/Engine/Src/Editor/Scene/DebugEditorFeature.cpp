#include "pch.h"

#ifdef CORE_EDITOR

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
        if (debugEditor_) {
            debugEditor_->ClearHistory();
        }
        // 破棄するとエンジン常駐の UI への結びつきとパネルの登録が外れる
        debugEditor_.reset();
    }
}

#endif // CORE_EDITOR
