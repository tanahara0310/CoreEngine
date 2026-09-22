#pragma once

#include "Graphics/Render/Line/ILineSource.h"

namespace CoreEngine
{
class PhysicsWorld;

/// @brief 剛体の速度・接触点・眠りのワイヤ表示（`sys.Physics.DebugDraw` で切り替え）。
/// @details `PhysicsFeature` が所有して Line パスへ登録し、`PhysicsWorld` の状態を毎フレーム描く。
class PhysicsDebugRenderer : public ILineSource {
public:
    /// @brief 描画対象の物理ワールドを設定する
    void SetWorld(const PhysicsWorld* world) { world_ = world; }

    /// @brief Line パスから呼ばれ、物理の状態を線で供給する
    void SubmitLines(LineRendererPipeline& pipeline, const Camera* camera) override;

private:
    const PhysicsWorld* world_ = nullptr;
};
}
