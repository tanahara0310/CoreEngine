#pragma once

#ifdef USE_IMGUI

#include "ISceneFeature.h"
#include <memory>

namespace CoreEngine
{
    class GridRenderer;

    /// @brief エディタ用グリッド床を管理する Feature（デバッグビルド専用）。
    /// @details GridRenderer の実体は RenderManager が Grid パスとして所有する。
    ///          この Feature はそれを引いてきて表示を有効にし、DockingUI の
    ///          表示トグルを毎フレーム反映する。あわせて Y 軸ライン供給元として
    ///          Line パスへ登録する（垂直な軸だけは解析グリッドでは描けないため）。
    class GridFeature : public ISceneFeature {
    public:
        GridFeature();
        ~GridFeature() override;

        const char* GetName() const override { return "Grid"; }

        void Initialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す（止めるとグリッドの表示切り替えが効かなくなる）
        bool RunsWhileStopped() const override { return true; }
        void Finalize(SceneContext& ctx) override;

    private:
        /// グリッド本体（所有者は RenderManager。ここは参照するだけ）
        GridRenderer* gridRenderer_ = nullptr;
    };
}

#endif // USE_IMGUI
