#pragma once

#include "GameObject/GameObject.h"

namespace CoreEngine
{
class CollisionWorld;

/// @brief コライダーのワイヤ表示
/// @details **`Collider` 側はレンダラを一切知らない**。以前は `AABBCollider` が
///          `LineRendererPipeline` / `Camera` を直接 include して描画メソッドを
///          持っていた（しかも `_DEBUG` 限定で誰も呼んでいない死コードだった）。
///          描く側がコライダーを見に行く向きへ反転させてある。
///
///          `CollisionFeature` が生成し、`CollisionWorld` の登録内容を毎フレーム描く。
///          表示は CVar `r.Collision.DebugDraw` で切り替える。
class ColliderDebugRenderer : public GameObject {
public:
    const char* GetObjectName() const override { return "ColliderDebug"; }

    /// @brief 描画対象の衝突ワールドを設定する
    void SetWorld(const CollisionWorld* world) { world_ = world; }

    void Update() override {}
    void Draw(const Camera* camera) override;

    RenderPassType GetRenderPassType() const override { return RenderPassType::Line; }
    BlendMode GetBlendMode() const override { return BlendMode::kBlendModeNormal; }

    /// @brief ワールド空間での位置を返す
    /// @return 常に原点。ラインは各頂点がワールド座標を持つため位置を持たない。
    Vector3 GetWorldPosition() const override { return {}; }

private:
    const CollisionWorld* world_ = nullptr;
};
}
