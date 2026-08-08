#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Math/EulerTransform.h"

namespace CoreEngine
{
/// @brief スプライト・パーティクルエミッタ用の軽量トランスフォーム（素の `EulerTransform` を内包）。
/// @details GPU 定数バッファや親子階層を持たない点が `TransformComponent` との違い。
///          編集は `ITransformSource` 経由でギズモ・インスペクタから共通に行える。
class EulerTransformComponent : public IComponent, public ITransformSource {
public:
    const char* GetTypeName() const override { return "EulerTransform"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "トランスフォーム"; }
#endif

    // ===== ITransformSource =====

    Vector3& Translate() override { return transform_.translate; }
    Vector3& Rotate()    override { return transform_.rotate; }
    Vector3& Scale()     override { return transform_.scale; }

    // ===== アクセサ =====

    EulerTransform& Get() { return transform_; }
    const EulerTransform& Get() const { return transform_; }

private:
    EulerTransform transform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
};
}
