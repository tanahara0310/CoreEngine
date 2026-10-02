#pragma once

#include "GameObject/Component/Animation/AnimatorComponent.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Core/ObjectRef.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Math/Vector/Vector3.h"
#include "Reflection/Reflect.h"

#include <string>

namespace CoreEngine
{
/// @brief 他オブジェクトのジョイントへ自分を追従させる（武器の手持ち・手からのパーティクル等）。
/// @details `LateUpdate()` で追従するため生成順に依存しない。ジョイントのスケールは
///          引き継がない（Mixamo リグは 0.01 倍が入っており、そのまま掛けると消える）。
///          追従元はオブジェクトの ID で持つので、保存でき、追従元が消えれば追従をやめる。
class SkeletonSocketComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "SkeletonSocket"; }

    // 回転はラジアンで持ち、インスペクタでは度で見せる
    REFLECT_BEGIN(SkeletonSocketComponent, "ソケット追従")
        REFLECT_OBJECT_REF(animator_, "追従元")
        REFLECT_PROPERTY(jointName_, "ジョイント名", p.tooltip = "例: mixamorig:RightHand")
        REFLECT_PROPERTY(offsetTranslate_, "位置のずれ", p.range = Speed(0.01f))
        REFLECT_PROPERTY(offsetRotate_, "回転のずれ", p.range = Speed(0.01f), p.displayScale = kDegreesPerRadian)
        REFLECT_PROPERTY(offsetScale_, "スケール", p.range = Speed(0.01f))
    REFLECT_END()

    // ===== 設定 =====

    /// @brief 追従先を指定する
    /// @param animator 追従元の `AnimatorComponent`（所有権は持たない。nullptr で解除）
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    void Attach(AnimatorComponent* animator, const std::string& jointName) {
        animator_.Set(animator);
        jointName_ = jointName;
    }

    /// @brief ジョイントから見た相対姿勢（ソケットオフセット）を設定する
    /// @param translate ジョイントローカルでの位置ずらし [m]
    /// @param rotate    ジョイントローカルでの回転（ラジアン）
    /// @param scale     スケール
    void SetOffset(const Vector3& translate, const Vector3& rotate,
                   const Vector3& scale = { 1.0f, 1.0f, 1.0f }) {
        offsetTranslate_ = translate;
        offsetRotate_ = rotate;
        offsetScale_ = scale;
    }

    /// @brief 追従が有効か（追従元とジョイント名が揃っているか）
    bool IsAttached() const { return animator_.Get() != nullptr && !jointName_.empty(); }

    // ===== ライフサイクル =====

    void Start() override { transform_ = Sibling<TransformComponent>(); }

    /// @brief トランスフォームを使う
    bool RequiresComponent(const IComponent& other) const override
    {
        return dynamic_cast<const TransformComponent*>(&other) != nullptr;
    }

    /// @brief ジョイントのワールド行列にオフセットを掛けて自分のワールド行列を上書きする
    /// @note `LateUpdate()` なのは追従元のアニメーション更新（`AnimatorComponent::Update()`）が
    ///       全オブジェクト分終わった後に読む必要があるため。
    void LateUpdate() override;

private:
    /// 追従元（所有権は持たない。使うたびに ID から引き直す）
    ObjectRef<AnimatorComponent> animator_;

    /// 追従先のジョイント名
    std::string jointName_;

    // ソケットオフセット（ジョイントローカル）
    Vector3 offsetTranslate_{ 0.0f, 0.0f, 0.0f };
    Vector3 offsetRotate_{ 0.0f, 0.0f, 0.0f };
    Vector3 offsetScale_{ 1.0f, 1.0f, 1.0f };

    TransformComponent* transform_ = nullptr;
};
}
