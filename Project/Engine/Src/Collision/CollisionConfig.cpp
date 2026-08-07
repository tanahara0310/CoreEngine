#include "pch.h"
#include "CollisionConfig.h"


namespace CoreEngine
{
CollisionConfig::CollisionConfig() {
   // デフォルトはすべて無効化
   for (int i = 0; i < kMaxLayers; ++i) {
      for (int j = 0; j < kMaxLayers; ++j) {
         matrix_[i][j] = false;
      }
   }

   // Default層のみ全てのレイヤーと衝突（汎用的な設定）
   // Default×Default も含める。レイヤーを指定せずに（既定引数のまま）コライダーを
   // 付けたオブジェクト同士が当たらないと、既定の使い方でいきなり動かないため。
   for (int i = 0; i < kMaxLayers; ++i) {
      SetCollisionEnabled(CollisionLayer::Default, static_cast<CollisionLayer>(i), true);
   }
}

void CollisionConfig::SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable) {
   int ia = static_cast<int>(a);
   int ib = static_cast<int>(b);
   matrix_[ia][ib] = enable;
   matrix_[ib][ia] = enable;

   // ビットマスクを常にマトリクスと同期させる（片方だけ更新すると食い違う）
   const uint64_t bitA = uint64_t{ 1 } << ia;
   const uint64_t bitB = uint64_t{ 1 } << ib;
   if (enable) {
      layerMasks_[ia] |= bitB;
      layerMasks_[ib] |= bitA;
   } else {
      layerMasks_[ia] &= ~bitB;
      layerMasks_[ib] &= ~bitA;
   }
}

bool CollisionConfig::IsCollisionEnabled(CollisionLayer a, CollisionLayer b) const {
   int ia = static_cast<int>(a);
   int ib = static_cast<int>(b);
   return matrix_[ia][ib];
}

uint64_t CollisionConfig::GetLayerMask(CollisionLayer layer) const {
   return layerMasks_[static_cast<int>(layer)];
}
}
