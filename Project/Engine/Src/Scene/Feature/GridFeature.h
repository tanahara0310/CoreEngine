#pragma once

#ifdef USE_IMGUI

#include "ISceneFeature.h"

namespace CoreEngine
{
    class GridRenderer;

    /// @brief デバッグ用グリッド床を管理する Feature（デバッグビルド専用）
    /// @details Initialize でグリッドを生成し、FrameStart で DockingUI の
    ///          表示トグルを反映する。所有権は GameObjectManager。
    class GridFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "Grid"; }

        void Initialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;
        void Finalize(SceneContext& ctx) override;

    private:
        // グリッドレンダラー（所有権は GameObjectManager）
        GridRenderer* gridRenderer_ = nullptr;
    };
}

#endif // USE_IMGUI
