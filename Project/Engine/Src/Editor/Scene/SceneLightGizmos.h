#pragma once

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorImage.h"
#include "Utility/Lifetime/ScopedRegistration.h"

#include <array>

namespace CoreEngine
{
    class GraphicsCore;
}

namespace CoreEngine::Editor
{
    struct SceneViewContext;

    /// @brief シーンビューにライトのアイコンと、選んだライトの届く範囲を描く
    /// @details アイコンは種類ごとの画像（`Engine/Assets/Textures/Icon/Light`）にライトの色を掛け、
    ///          ライトを持つオブジェクトの位置へ同じ大きさで出す。アイコンをクリックするとそのオブジェクトを選ぶ。
    ///          範囲の線は選んでいるライトだけに出し、物に隠れた所は薄く描く。描くかどうかは CVar `d.Light.Visualize`。
    class SceneLightGizmos
    {
    public:
        /// @brief アイコンの画像を GPU へ載せ、シーンビューの道具として登録する
        /// @details 最初のフレームより前に呼ぶ
        /// @param graphics アイコンの画像を置く GPU（ImGui と同じディスクリプタヒープを使うもの）
        void Initialize(GraphicsCore& graphics);

    private:
        /// @brief シーンビューを描くフレームごとに呼ばれる
        void Draw(const SceneViewContext& context);

        /// LightType の並びと同じ順のアイコン（読めなかったものは id = 0）
        std::array<EditorImage, 4> icons_{};

        // シーンビューの道具の登録（破棄すると外れる。ほかのメンバより先に破棄される）
        ScopedRegistration registration_;
    };
}

#endif // CORE_EDITOR
