#include "SphereObject.h"

#ifdef _DEBUG
#include "externals/imgui/imgui.h"
#endif

using namespace CoreEngine;

void SphereObject::SetMaterialColor(const Vector4& color) {
    defaultColor_ = color;
    if (model_) model_->GetMaterial()->SetColor(color);
}

void SphereObject::OnCollisionEnter([[maybe_unused]] GameObject* other) {
    if (++hitCount_ == 1) {
        model_->GetMaterial()->SetColor(hitColor_);
    }
}

void SphereObject::OnCollisionStay([[maybe_unused]] GameObject* other) {
    // Enter でカラー変更済みのため何もしない
}

void SphereObject::OnCollisionExit([[maybe_unused]] GameObject* other) {
    if (--hitCount_ <= 0) {
        hitCount_ = 0;
        model_->GetMaterial()->SetColor(defaultColor_);
    }
}

