#include "pch.h"
#include "WalkModelObject.h"

#include "Particle/IParticleSystem.h"

void WalkModelObject::OnInitialize() {
    transform_.translate = { 3.0f, 0.0f, 0.0f };  // 右側に配置
    transform_.scale = { 1.0f, 1.0f, 1.0f };
    transform_.rotate = { 0.0f, 0.0f, 0.0f };
}

void WalkModelObject::SetHandParticleSystem(CoreEngine::IParticleSystem* system, const std::string& jointName) {
    handParticleSystem_ = system;
    handJointName_ = jointName;
}

void WalkModelObject::OnAnimationUpdated() {
    if (!handParticleSystem_ || handJointName_.empty()) {
        return;
    }

    // スケルトンが今フレームの姿勢に更新された直後なので、
    // ここで取ったジョイント位置は表示される絵と完全に一致する。
    if (const auto handPosition = GetJointWorldPosition(handJointName_)) {
        handParticleSystem_->SetEmitterPosition(*handPosition);
    }
}
