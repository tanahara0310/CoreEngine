#pragma once
#include <array>
#include <cstdint>
#include "CollisionLayer.h"

/// @brief 衝突判定の設定を管理するクラス
/// @note レイヤー間の衝突可否をマトリクスで管理

namespace CoreEngine
{
class CollisionConfig {
public:
   CollisionConfig();

   /// @brief 指定したレイヤー間の衝突判定を有効/無効に設定
   /// @param a レイヤーA
   /// @param b レイヤーB
   /// @param enable true:衝突判定有効 / false:衝突判定無効
   void SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable);

   /// @brief 指定したレイヤー間の衝突判定が有効かどうかを確認
   /// @param a レイヤーA
   /// @param b レイヤーB
   /// @return true:衝突判定有効 / false:衝突判定無効
   bool IsCollisionEnabled(CollisionLayer a, CollisionLayer b) const;

   /// @brief 指定レイヤーが衝突しうる相手レイヤーのビットマスクを取得
   /// @param layer 基準となるレイヤー
   /// @return bit n が立っていれば CollisionLayer(n) と衝突しうる
   /// @details ブロードフェーズの前段で「そもそも当たりうるか」をビット演算 1 回で
   ///          落とすために使う。マトリクスを 2 次元添字で引くより速く、
   ///          形状の AABB 判定より先に弾ける。
   uint64_t GetLayerMask(CollisionLayer layer) const;

   /// @brief レイヤー数（マスクのビット幅の上限チェック用）
   static constexpr int kMaxLayers = static_cast<int>(CollisionLayer::Count);
   static_assert(kMaxLayers <= 64, "CollisionLayer が 64 を超えると uint64_t のマスクに収まらない");

private:
   std::array<std::array<bool, kMaxLayers>, kMaxLayers> matrix_;

   /// 行ごとのビットマスク（matrix_ と常に同期させる）
   std::array<uint64_t, kMaxLayers> layerMasks_{};
};
}
