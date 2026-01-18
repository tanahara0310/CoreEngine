#pragma once

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
}
