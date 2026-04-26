#pragma once
#include <cstdint>

namespace CoreEngine
{
    /// @brief タイルの属性（当たり判定・ゲームロジック用）
    /// @note 16以上はユーザー定義として自由に使用可能
    enum class TileAttribute : uint8_t
    {
        Empty  = 0,  ///< 通れる（床・草など）
        Solid  = 1,  ///< 通れない（壁・地面）
        Water  = 2,  ///< 水（速度変化などゲーム側で処理）
        Ladder = 3,  ///< はしご
        Damage = 4,  ///< ダメージ床
        // 16 以降: ユーザー定義
    };

    /// @brief タイルIDから当たり判定があるか判定するデフォルト実装
    /// @note ゲーム側で独自ルールが必要な場合は TileMap::SetSolidChecker() で上書きする
    inline bool IsDefaultSolid(uint8_t tileId)
    {
        return tileId == static_cast<uint8_t>(TileAttribute::Solid);
    }

} // namespace CoreEngine
