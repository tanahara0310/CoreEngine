#pragma once

#ifdef USE_IMGUI

#include "ISceneFeature.h"
#include <memory>

namespace CoreEngine
{
    class SceneDebugEditor;

    /// @brief シーンのデバッグ編集機能（Undo/Redo・ギズモ・Hierarchy/Inspector）を
    ///        管理する Feature（デバッグビルド専用）
    /// @details SceneDebugEditor は Initialize 内で DockingUI / GameDebugUI へ
    ///          自己登録するため、本 Feature は生成とライフサイクル管理のみを行う。
    class DebugEditorFeature : public ISceneFeature {
    public:
        // SceneDebugEditor が前方宣言のため、コンストラクタ/デストラクタは cpp 側で定義する
        DebugEditorFeature();
        ~DebugEditorFeature() override;

        const char* GetName() const override { return "DebugEditor"; }

        void Initialize(SceneContext& ctx) override;
        void Update(SceneContext& ctx, SceneUpdatePhase phase) override;

        /// @brief 停止中も回す（止めるとギズモも選択も動かなくなる。停止中の主役）
        bool RunsWhileStopped() const override { return true; }
        void Finalize(SceneContext& ctx) override;

    private:
        std::unique_ptr<SceneDebugEditor> debugEditor_;
    };
}

#endif // USE_IMGUI
