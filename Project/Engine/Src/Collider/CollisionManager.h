#pragma once
#include <vector>
#include <unordered_set>
#include "Collider.h"
#include "CollisionConfig.h"

/// @brief 衝突判定を一括管理するマネージャークラス
/// @note 登録されたすべてのコライダー間の衝突判定を行い、適切なコールバックを実行

namespace CoreEngine
{
class CollisionManager {
public:
   explicit CollisionManager(CollisionConfig* config);
   ~CollisionManager() = default;

   /// @brief コライダーを登録
   /// @param collider 登録するコライダー
   void RegisterCollider(Collider* collider);

   /// @brief 登録されているすべてのコライダー間の衝突判定を実行
   void CheckAllCollisions();

   /// @brief 登録されているコライダーをすべてクリア（previousCollisions_は保持）
   void ClearColliders();

   /// @brief 登録されているコライダーと衝突履歴をすべてクリア（シーン遷移時など）
   void Clear();

   /// @brief 登録されているすべてのコライダーを取得
   /// @return コライダーのリスト
   const std::vector<Collider*>& GetAllColliders() const { return colliders_; }

   struct ColliderPairHash {
      size_t operator()(const std::pair<Collider*, Collider*>& p) const noexcept {
         return reinterpret_cast<size_t>(p.first) ^ reinterpret_cast<size_t>(p.second);
      }
   };

private:
   std::vector<Collider*> colliders_;
   CollisionConfig* config_ = nullptr;

   // 前フレームの衝突ペアを記録（Enter/Stay/Exitの判定用）
   std::unordered_set<std::pair<Collider*, Collider*>, ColliderPairHash> previousCollisions_;
};
}
