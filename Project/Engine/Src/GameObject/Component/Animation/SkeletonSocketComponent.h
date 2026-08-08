#pragma once

#include "GameObject/Component/Animation/AnimatorComponent.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Math/Vector/Vector3.h"

#include <string>

namespace CoreEngine
{
/// @brief 他オブジェクトのジョイントへ自分を追従させる（武器の手持ち・手からのパーティクル等）。
/// @details `LateUpdate()` で追従するため生成順に依存しない。ジョイントのスケールは
///          引き継がない（Mixamo リグは 0.01 倍が入っており、そのまま掛けると消える）。
class SkeletonSocketComponent : public IComponent {
public:
    const char* GetTypeName() const override { return "SkeletonSocket"; }

#ifdef USE_IMGUI
    const char* GetInspectorName() const override { return "ソケット追従"; }
#endif

    // ===== 設定 =====

    /// @brief 追従先を指定する
    /// @param animator 追従元の `AnimatorComponent`（所有権は持たない。nullptr で解除）
    /// @param jointName ジョイント名（例: "mixamorig:RightHand"）
    void Attach(const AnimatorComponent* animator, const std::string& jointName) {
        animator_ = animator;
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
    bool IsAttached() const { return animator_ != nullptr && !jointName_.empty(); }

    // ===== ライフサイクル =====

    void Start() override { transform_ = Sibling<TransformComponent>(); }

    /// @brief ジョイントのワールド行列にオフセットを掛けて自分のワールド行列を上書きする
    /// @note `LateUpdate()` なのは追従元のアニメーション更新（`AnimatorComponent::Update()`）が
    ///       全オブジェクト分終わった後に読む必要があるため。
    void LateUpdate() override;

private:
    /// 追従元（所有権は持たない）
    const AnimatorComponent* animator_ = nullptr;

    /// 追従先のジョイント名
    std::string jointName_;

    // ソケットオフセット（ジョイントローカル）
    Vector3 offsetTranslate_{ 0.0f, 0.0f, 0.0f };
    Vector3 offsetRotate_{ 0.0f, 0.0f, 0.0f };
    Vector3 offsetScale_{ 1.0f, 1.0f, 1.0f };

    TransformComponent* transform_ = nullptr;
};
}
