#pragma once

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief UI の描く順を親子に合わせて並べ直す
    /// @details 親を指している UI が 1 つでもあれば、親の無い UI を描画順（同じならヒエラルキーの順）で並べ、
    ///          子をそれぞれの親のすぐ後に、兄弟の間は描画順（同じならヒエラルキーの順）で並べる。
    ///          親を指している UI が無ければ、どの UI も自分の描画順のまま。
    /// @param manager 並べ直すシーンのオブジェクト
    /// @param resolved 前のフレームで並べ直したか（並べ直したら true、元の描画順に戻したら false にする）
    void ResolveUIDrawOrder(GameObjectManager& manager, bool& resolved);
}
