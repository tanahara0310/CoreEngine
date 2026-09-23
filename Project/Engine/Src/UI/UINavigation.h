#pragma once

#include "Math/Vector/Vector2.h"
#include "UI/UIElement.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief フォーカスを送る向き
    enum class UINavigationDirection : std::uint8_t
    {
        Up = 0,
        Down,
        Left,
        Right,
    };

    /// @brief 向きで次の UI を選ぶための計算
    /// @details キャンバス座標（左上が原点・Y は下が正）で考える。
    ///          回っている要素も矩形の中心で見る。
    namespace UINavigation
    {
        /// @brief 向きの単位ベクトル
        Vector2 ToVector(UINavigationDirection direction);

        /// @brief 矩形の、向いている側の辺の中心
        /// @details 送り出す起点。中心から測ると、縦に長い要素の横にあるものが
        ///          実際より遠く見えてしまう。
        Vector2 EdgePoint(const UIRect& rect, const Vector2& direction);

        /// @brief 矩形の中心
        Vector2 Center(const UIRect& rect);

        /// @brief 送り先としての点数
        /// @param from 送り出す起点
        /// @param direction 向きの単位ベクトル
        /// @param target 送り先の中心
        /// @return 大きいほど良い候補。0 なら候補にしない（向きの後ろにある）
        /// @note 向きの合い具合を距離で割る。近くて真っ直ぐなものほど高くなる。
        float Score(const Vector2& from, const Vector2& direction, const Vector2& target);
    }
}
