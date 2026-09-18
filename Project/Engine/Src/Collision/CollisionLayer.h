#pragma once

#include <cstddef>
#include <iterator>
#include <string_view>

/// @brief 衝突判定レイヤー
/// @note 衝突判定の最適化とゲームロジックの分離に使用

namespace CoreEngine
{
enum class CollisionLayer {
   Default = 0,   // デフォルトレイヤー（汎用）
   Player,        // プレイヤー
   Enemy,         // 敵
   PlayerBullet,  // プレイヤーの弾
   EnemyBullet,   // 敵の弾
   Boss,          // ボス
   BossBullet,    // ボスの弾
   BossAttack,    // ボスの攻撃判定
   Item,          // アイテム
   Environment,   // 環境オブジェクト（壁など）
   Count          // レイヤー数（列挙の最後に配置）
};

/// @brief レイヤーの名前（列挙の並びと同じ順。保存データとスクリプトが使う）
inline constexpr const char* kCollisionLayerNames[] = {
   "Default", "Player", "Enemy", "PlayerBullet", "EnemyBullet",
   "Boss", "BossBullet", "BossAttack", "Item", "Environment",
};
static_assert(std::size(kCollisionLayerNames) == static_cast<std::size_t>(CollisionLayer::Count),
   "CollisionLayer を増減したら kCollisionLayerNames も更新すること");

/// @brief レイヤーの名前（範囲外なら "Default"）
inline const char* ToString(CollisionLayer layer)
{
   const auto index = static_cast<std::size_t>(layer);
   return index < std::size(kCollisionLayerNames) ? kCollisionLayerNames[index] : kCollisionLayerNames[0];
}

/// @brief 名前からレイヤーを引く
/// @return 名前が無ければ false（out は変えない）
inline bool TryParseCollisionLayer(std::string_view name, CollisionLayer& out)
{
   for (std::size_t i = 0; i < std::size(kCollisionLayerNames); ++i) {
      if (name == kCollisionLayerNames[i]) {
         out = static_cast<CollisionLayer>(i);
         return true;
      }
   }
   return false;
}
}
