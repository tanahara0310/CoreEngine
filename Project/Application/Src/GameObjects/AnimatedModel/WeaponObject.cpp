#include "pch.h"
#include "WeaponObject.h"

#include "Graphics/Primitive/CubeMeshGenerator.h"

using namespace CoreEngine;

namespace {
    // 剣の刃を模した細長い箱のサイズ [m]（Y 軸方向が刃の長さ）
    constexpr float kBladeWidth = 0.05f;
    constexpr float kBladeLength = 0.75f;
    constexpr float kBladeThickness = 0.015f;
}

void WeaponObject::AttachToJoint(const AnimatedModelObject* owner, const std::string& jointName) {
    socket_->Attach(owner ? owner->GetAnimator() : nullptr, jointName);
}

void WeaponObject::SetSocketOffset(const Vector3& translate, const Vector3& rotate, const Vector3& scale) {
    socket_->SetOffset(translate, rotate, scale);
}

std::unique_ptr<IPrimitiveMeshGenerator> WeaponObject::CreateMeshGenerator() const {
    return std::make_unique<CubeMeshGenerator>(kBladeWidth, kBladeLength, kBladeThickness);
}
