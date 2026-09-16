#pragma once

namespace CoreEngine::Editor
{
    /// @brief 標準レイアウトのどの区画へ置くか
    /// @details どのパネルがどこへ行くかは記述子の `defaultDock` だけが決める。
    enum class DockArea
    {
        None,       ///< ドックしない（Window メニューから開くフローティング）
        LeftTop,    ///< 左上 — Hierarchy
        LeftBottom, ///< 左下 — Project
        Center,     ///< 中央 — Scene / Game / Canvas
        Right,      ///< 右 — Inspector
        Bottom,     ///< 下 — Console / Profiler
    };

    /// @brief 区画の数（`DockArea` を添字にする配列の大きさ）
    inline constexpr int kDockAreaCount = 6;
}
