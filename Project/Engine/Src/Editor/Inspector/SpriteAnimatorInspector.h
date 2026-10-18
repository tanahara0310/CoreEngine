#pragma once

#ifdef CORE_EDITOR

namespace CoreEngine
{
class SpriteAnimatorComponent;

/// @brief Inspector のスプライトアニメのクリップ一覧の編集セクション
namespace SpriteAnimatorInspector
{
    /// @brief クリップの一覧と編集 UI を描画する
    /// @return 値が変更されたら true
    bool Draw(SpriteAnimatorComponent& animator);
}
}

#endif // CORE_EDITOR
