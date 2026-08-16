#include "pch.h"
#include "SkeletonSocketComponent.h"

#include "Math/MathCore.h"

namespace CoreEngine
{
    void SkeletonSocketComponent::LateUpdate()
    {
        if (!IsAttached()) {
            return;
        }
        if (!transform_) {
            transform_ = Sibling<TransformComponent>();
            if (!transform_) { return; }
        }

        const std::optional<Matrix4x4> jointWorld = animator_->GetJointWorldMatrix(jointName_);
        if (!jointWorld) {
            return;
        }

        // ジョイント行列はスケールを含みうる（リグの単位系によっては 0.01 倍などになる）。
        // 追従物がボーンのスケールに引きずられると意図しない大きさになるため、
        // 位置と回転だけを取り出して使う。
        Vector3 jointScale{};
        Vector3 jointRotate{};
        Vector3 jointTranslate{};
        MathCore::Matrix::DecomposeToSRT(*jointWorld, jointScale, jointRotate, jointTranslate);

        const Matrix4x4 jointNoScale =
            MathCore::Matrix::MakeAffine({ 1.0f, 1.0f, 1.0f }, jointRotate, jointTranslate);

        // ソケット行列（ジョイントローカル）→ ジョイントのワールド行列 の順に掛ける。
        // 行ベクトル規約なので「子 → 親」の順で Multiply する。
        const Matrix4x4 socketLocal =
            MathCore::Matrix::MakeAffine(offsetScale_, offsetRotate_, offsetTranslate_);

        // TransformComponent::Update() が計算したワールド行列を上書きする。
        // LateUpdate は全オブジェクトの Update 完了後に走るので、追従元の
        // アニメーションは必ず今フレームの姿勢になっている（生成順に依存しない）。
        transform_->Get().SetWorldMatrix(socketLocal * jointNoScale);
    }
}
