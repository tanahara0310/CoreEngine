#pragma once
#include <array>
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

private:
   static constexpr int kMaxLayers = static_cast<int>(CollisionLayer::Count);
   std::array<std::array<bool, kMaxLayers>, kMaxLayers> matrix_;
};
}
