#pragma once

#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    class GameObjectManager;
    class RectTransformComponent;

    /// @brief UI を並べる部品・大きさを合わせる部品を、シーンの UI へ効かせる
    /// @details 子から親へ大きさを測り、親から子へ大きさと位置を書く。
    ///          値が変わった物だけを書き換える。部品が 1 つも無ければ何もしない。
    void ResolveUILayout(GameObjectManager& manager);

    namespace UILayoutPass
    {
        /// @brief 並べる処理が書き換える、UI トランスフォームの値
        struct RectState
        {
            Vector2 anchorMin;
            Vector2 anchorMax;
            Vector2 size;
            Vector2 anchoredPosition;

            /// @brief UI トランスフォームの今の値
            static RectState Of(const RectTransformComponent& rect);

            /// @brief UI トランスフォームへ書き戻す
            void ApplyTo(RectTransformComponent& rect) const;

            bool operator==(const RectState&) const = default;
        };

        /// @brief 並べる処理が UI トランスフォームを書き換えたときに呼ぶ関数
        /// @param before 書き換える前の値
        using RectChanged = void (*)(RectTransformComponent& rect, const RectState& before);

        /// @brief 並べる処理が UI トランスフォームを書き換えたときに呼ぶ関数を設定する（nullptr で外す）
        void SetRectChangedCallback(RectChanged callback);

        /// @brief 並べる部品・大きさを合わせる部品が生まれた
        void NotifyCreated();

        /// @brief 並べる部品・大きさを合わせる部品が消えた
        void NotifyDestroyed();

        /// @brief 並べる部品・大きさを合わせる部品が 1 つでもあるか
        bool AnyAlive();
    }
}
