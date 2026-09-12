#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Transform/ITransformSource.h"
#include "Math/EulerTransform.h"
#include "Reflection/Reflect.h"

namespace CoreEngine
{
/// @brief スプライト・パーティクルエミッタ用の軽量トランスフォーム（素の `EulerTransform` を内包）。
/// @details GPU 定数バッファや親子階層を持たない点が `TransformComponent` との違い。
///          編集は `ITransformSource` 経由でギズモ・インスペクタから共通に行える。
class EulerTransformComponent : public IComponent, public ITransformSource {
public:
    const char* GetTypeName() const override { return "EulerTransform"; }

    REFLECT_BEGIN(EulerTransformComponent, "トランスフォーム")
        REFLECT_PROPERTY(transform_.translate, "位置",     p.range = Speed(0.05f))
        REFLECT_PROPERTY(transform_.rotate,    "回転",     p.range = Speed(0.01f))
        REFLECT_PROPERTY(transform_.scale,     "スケール", p.range = Speed(0.01f))
    REFLECT_END()

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
