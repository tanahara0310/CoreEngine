#pragma once

#ifdef CORE_EDITOR

#include "Editor/Environment/SunMoonController.h"
#include "Utility/Lifetime/ScopedRegistration.h"

#include <optional>

namespace CoreEngine::Editor
{
    struct SceneViewContext;
}

namespace CoreEngine
{
    /// @brief シーンビューに太陽と月の位置を重ね、ドラッグで動かせるようにする
    /// @details 空の物か太陽・月のライトを選んでいる間、カメラの周りの空に地平線・方角・太陽の 1 日の軌道と、
    ///          太陽と月の印を描く。画面の外にある天体は、画面の端にその向きの矢印を出す。
    ///          印を左ドラッグすると、カーソルの向きの空へ動かす。描くかどうかは CVar `d.Atmosphere.SceneGizmo`。
    ///          シーンビューを描くたびに、そのカメラの向きと画角を controller へ渡す（空の図が見ている範囲を描くのに使う）。
    class SunMoonSceneGizmo
    {
    public:
        /// @brief シーンビューの道具として登録する
        void Initialize(SunMoonController& controller);

        /// @brief シーンビューに描くか
        static bool IsEnabled();
        static void SetEnabled(bool enabled);

    private:
        /// @brief シーンビューを描くフレームごとに呼ばれる
        void Draw(const Editor::SceneViewContext& context);

        SunMoonController* controller_ = nullptr;
        std::optional<SkyBody> dragging_; ///< ドラッグしている天体

        // シーンビューの道具の登録（破棄すると外れる。ほかのメンバより先に破棄される）
        ScopedRegistration registration_;
    };
}

#endif // CORE_EDITOR
