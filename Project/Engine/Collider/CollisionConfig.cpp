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
   for (int i = 0; i < kMaxLayers; ++i) {
	  if (i != static_cast<int>(CollisionLayer::Default)) {
		 SetCollisionEnabled(CollisionLayer::Default, static_cast<CollisionLayer>(i), true);
	  }
   }
}

void CollisionConfig::SetCollisionEnabled(CollisionLayer a, CollisionLayer b, bool enable) {
   int ia = static_cast<int>(a);
   int ib = static_cast<int>(b);
   matrix_[ia][ib] = enable;
   matrix_[ib][ia] = enable;
}

bool CollisionConfig::IsCollisionEnabled(CollisionLayer a, CollisionLayer b) const {
   int ia = static_cast<int>(a);
   int ib = static_cast<int>(b);
   return matrix_[ia][ib];
}
}
