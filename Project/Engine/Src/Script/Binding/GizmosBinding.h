#pragma once

class asIScriptEngine;

namespace CoreEngine::Script
{
    /// @brief シーンビューに目印を描く `Gizmos` 名前空間をスクリプトへ登録する
    /// @details コンポーネントの OnDrawGizmos / OnDrawGizmosSelected の中で使う。
    ///          書き出したゲームでも呼べるが、何も描かない。
    /// @return すべて登録できたら true
    bool RegisterGizmosBinding(asIScriptEngine* engine);

#ifdef CORE_EDITOR
    /// @brief コンポーネントのギズモの関数を呼ぶ間だけ、Gizmos で描けるようにする（色は白から始める）
    class GizmoDrawScope
    {
    public:
        GizmoDrawScope();
        ~GizmoDrawScope();

        GizmoDrawScope(const GizmoDrawScope&) = delete;
        GizmoDrawScope& operator=(const GizmoDrawScope&) = delete;
    };
#endif
}
