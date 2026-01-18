#include "CollisionManager.h"
#include <algorithm>


namespace CoreEngine
{
namespace {
   // コライダーペアを一意にするためのヘルパー関数
   std::pair<Collider*, Collider*> MakePair(Collider* a, Collider* b) {
      return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
   }
}

CollisionManager::CollisionManager(CollisionConfig* config)
   : config_(config) {
}

void CollisionManager::RegisterCollider(Collider* collider) {
   if (collider) colliders_.push_back(collider);
}

void CollisionManager::CheckAllCollisions() {
   std::unordered_set<std::pair<Collider*, Collider*>, ColliderPairHash> currentCollisions;

   // すべてのコライダーペアをチェック
   for (size_t i = 0; i < colliders_.size(); ++i) {
      for (size_t j = i + 1; j < colliders_.size(); ++j) {
         Collider* a = colliders_[i];
         Collider* b = colliders_[j];

         // コライダーが無効化されている場合はスキップ
         if (!a->IsEnabled() || !b->IsEnabled()) continue;

         // コリジョンマトリクスで判定が無効なら処理しない
         if (!config_->IsCollisionEnabled(a->GetLayer(), b->GetLayer())) continue;

         auto pair = MakePair(a, b);
         bool isColliding = a->CheckCollision(b);

         if (isColliding) {
            currentCollisions.insert(pair);

            // 前フレームで衝突していなかった場合、Enter
            if (previousCollisions_.find(pair) == previousCollisions_.end()) {
               a->OnCollisionEnter(b);
               b->OnCollisionEnter(a);
            } else {
               // 前フレームも衝突していた場合、Stay
               a->OnCollisionStay(b);
               b->OnCollisionStay(a);
            }
         } else {
            // 前フレームで衝突していたが今フレームは離れた場合、Exit
            if (previousCollisions_.find(pair) != previousCollisions_.end()) {
               a->OnCollisionExit(b);
               b->OnCollisionExit(a);
            }
         }
      }
   }

   previousCollisions_ = std::move(currentCollisions);
}

void CollisionManager::Clear() {
   colliders_.clear();
   previousCollisions_.clear();
}
}
