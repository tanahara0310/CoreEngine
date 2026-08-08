#pragma once

#ifdef USE_IMGUI

#include "ISceneFeature.h"
#include <memory>

namespace CoreEngine
{
    class GridRenderer;

    /// @brief エディタ用グリッド床を管理する Feature（デバッグビルド専用）。
    /// @details GridRenderer（ILineSource）を所有して Line パスへ登録し、
    ///          DockingUI の表示トグルを毎フレーム反映する。
    class GridFeature : public ISceneFeature {
    public:
        GridFeature();
        ~GridFeature() override;   // unique_ptr<GridRenderer> のため .cpp で定義（前方宣言のみで足りる）

        const char* GetName() const override { return "Grid"; }

        void Initialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;
        void Finalize(SceneContext& ctx) override;

    private:
        /// グリッド本体（この Feature が所有。Line パスにはポインタ登録するだけ）
        std::unique_ptr<GridRenderer> gridRenderer_;
    };
}

#endif // USE_IMGUI
