#pragma once

#include "Graphics/Render/Line/ILineSource.h"

namespace CoreEngine
{
class CollisionWorld;

/// @brief コライダーのワイヤ表示（`r.Collision.DebugDraw` で切り替え）。
/// @details GameObject ではなく `ILineSource`。CollisionFeature が所有して Line パスへ登録し、
///          `CollisionWorld` の登録内容を毎フレーム描く。`Collider` 側はレンダラを一切知らない。
class ColliderDebugRenderer : public ILineSource {
public:
    /// @brief 描画対象の衝突ワールドを設定する
    void SetWorld(const CollisionWorld* world) { world_ = world; }

    /// @brief Line パスから呼ばれ、コライダーのワイヤフレームを供給する
    void SubmitLines(LineRendererPipeline& pipeline, const Camera* camera) override;

private:
    const CollisionWorld* world_ = nullptr;
};
}
