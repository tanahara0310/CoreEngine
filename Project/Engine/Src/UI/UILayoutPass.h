#pragma once

namespace CoreEngine
{
    class GameObjectManager;

    /// @brief UI を並べる部品・大きさを合わせる部品を、シーンの UI へ効かせる
    /// @details 子から親へ大きさを測り、親から子へ大きさと位置を書く。
    ///          値が変わった物だけを書き換える。部品が 1 つも無ければ何もしない。
    void ResolveUILayout(GameObjectManager& manager);

    namespace UILayoutPass
    {
        /// @brief 並べる部品・大きさを合わせる部品が生まれた
        void NotifyCreated();

        /// @brief 並べる部品・大きさを合わせる部品が消えた
        void NotifyDestroyed();

        /// @brief 並べる部品・大きさを合わせる部品が 1 つでもあるか
        bool AnyAlive();
    }
}
