#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine
{
    class IComponent;
}

namespace CoreEngine::InspectorRenderer
{
    struct DrawContext;
}

/// @brief UI トランスフォームのインスペクタ（アンカーの 16 通りの格子と、伸ばした向きの余白）
namespace CoreEngine::Editor::RectTransformInspector
{
    /// @brief 欄を描く
    /// @details 先頭にアンカーの選択（Shift で基準点も、Alt で位置も合わせる）を置き、
    ///          アンカーを開いている向きは位置と大きさの代わりに親の端からの余白で見せる。
    /// @return 値を変えたら true
    bool Draw(IComponent& component, const InspectorRenderer::DrawContext& context);
}

#endif // CORE_EDITOR
