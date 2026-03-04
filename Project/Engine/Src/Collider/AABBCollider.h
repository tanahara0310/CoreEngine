#pragma once
#include "Collider.h"

// 前方宣言
namespace CoreEngine {
    class LineRendererPipeline;
    class ICamera;
}

namespace CoreEngine
{
class AABBCollider : public Collider {
public:
   AABBCollider(GameObject* owner, const Vector3& size);

   bool CheckCollision(Collider* other) const override;

   void SetSize(const Vector3& size) override;

   /// @brief AABB の最小座標を取得
   Vector3 GetMin() const;

   /// @brief AABB の最大座標を取得
   Vector3 GetMax() const;

   /// @brief サイズを取得
   Vector3 GetSize() const { return size_; }

#ifdef _DEBUG
   /// @brief デバッグ用にコライダーを描画
   /// @param pipeline LineRendererPipeline
   /// @param camera カメラ
   /// @param color ラインの色（デフォルト: 緑）
   void DrawDebug(CoreEngine::LineRendererPipeline* pipeline, const CoreEngine::ICamera* camera, 
                  const Vector3& color = {0.0f, 1.0f, 0.0f}) const;
#endif

private:
   Vector3 size_{};
};
}
