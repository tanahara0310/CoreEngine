#include "pch.h"
#include "WeaponObject.h"

#include "Graphics/Primitive/CubeMeshGenerator.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

using namespace CoreEngine;

namespace {
    // 剣の刃を模した細長い箱のサイズ [m]（Y 軸方向が刃の長さ）
    constexpr float kBladeWidth = 0.05f;
    constexpr float kBladeLength = 0.75f;
    constexpr float kBladeThickness = 0.015f;
}

void WeaponObject::AttachToJoint(const AnimatedModelObject* owner, const std::string& jointName) {
    owner_ = owner;
    jointName_ = jointName;
}

void WeaponObject::SetSocketOffset(const Vector3& translate, const Vector3& rotate, const Vector3& scale) {
    socketTranslate_ = translate;
    socketRotate_ = rotate;
    socketScale_ = scale;
}

std::unique_ptr<IPrimitiveMeshGenerator> WeaponObject::CreateMeshGenerator() const {
    return std::make_unique<CubeMeshGenerator>(kBladeWidth, kBladeLength, kBladeThickness);
}

void WeaponObject::OnUpdate() {
    if (!owner_ || jointName_.empty()) {
        return;
    }

    const std::optional<Matrix4x4> jointWorld = owner_->GetJointWorldMatrix(jointName_);
    if (!jointWorld) {
        return;
    }

    // ジョイント行列はスケールを含みうる（リグの単位系によっては 0.01 倍などになる）。
    // 武器がボーンのスケールに引きずられると意図しない大きさになるため、
    // 位置と回転だけを取り出して使う。
    Vector3 jointScale{};
    Vector3 jointRotate{};
    Vector3 jointTranslate{};
    MathCore::Matrix::DecomposeToSRT(*jointWorld, jointScale, jointRotate, jointTranslate);

    if (!loggedJointScale_) {
        loggedJointScale_ = true;
        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::General,
            "WeaponObject: joint='{}' scale=({:.4f},{:.4f},{:.4f}) pos=({:.3f},{:.3f},{:.3f})",
            jointName_, jointScale.x, jointScale.y, jointScale.z,
            jointTranslate.x, jointTranslate.y, jointTranslate.z);
    }

    const Matrix4x4 jointNoScale =
        MathCore::Matrix::MakeAffine({ 1.0f, 1.0f, 1.0f }, jointRotate, jointTranslate);

    // ソケット行列（ジョイントローカル）→ ジョイントのワールド行列 の順に掛ける。
    // 行ベクトル規約なので「子 → 親」の順で Multiply する。
    const Matrix4x4 socketLocal =
        MathCore::Matrix::MakeAffine(socketScale_, socketRotate_, socketTranslate_);

    // ModelGameObject::Update() が TransferMatrix() で計算したワールド行列を上書きする。
    // OnUpdate() は TransferMatrix() の後に呼ばれるので、こちらの値が最終的に使われる。
    transform_.SetWorldMatrix(MathCore::Matrix::Multiply(socketLocal, jointNoScale));
}
