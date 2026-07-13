#include "pch.h"
#include "ModelObject.h"


CoreEngine::BlendMode ModelObject::GetBlendMode() const {
    if (model_) {
        auto* mat = model_->GetMaterial();
        // α < 1 かつディザリングOFF → アルファブレンドで段階的透明
        // ディザリングON の場合は kBlendModeNone のままディザリングに任せる
        if (mat && mat->GetColor().w < 1.0f && !mat->IsDitheringEnabled()) {
            return CoreEngine::BlendMode::kBlendModeNormal;
        }
    }
    return CoreEngine::BlendMode::kBlendModeNone;
}

CoreEngine::MaterialInstance* ModelObject::GetMaterial() {
    return model_ ? model_->GetMaterial() : nullptr;
}

void ModelObject::SetPBRParameters(float metallic, float roughness, float occlusionStrength) {
    if (!model_) return;
    model_->ForEachMaterial([&](CoreEngine::MaterialInstance* mat) {
        mat->SetMetallic(metallic);
        mat->SetRoughness(roughness);
        mat->SetOcclusionStrength(occlusionStrength);
    });
}

void ModelObject::SetNormalMapEnabled(bool enable) {
    if (!model_) return;
    model_->ForEachMaterial([enable](CoreEngine::MaterialInstance* mat) {
        mat->SetNormalMapEnabled(enable);
    });
}

void ModelObject::SetMaterialColor(const CoreEngine::Vector4& color) {
    if (!model_) return;
    model_->ForEachMaterial([&color](CoreEngine::MaterialInstance* mat) {
        mat->SetColor(color);
    });
}

void ModelObject::SetIBLEnabled(bool enable) {
    // IBL の有効/無効はシーン側で決まるため、マテリアル側は強度によるオプトアウトで表現する
    SetIBLIntensity(enable ? 1.0f : 0.0f);
}

void ModelObject::SetIBLIntensity(float intensity) {
    if (!model_) return;
    model_->ForEachMaterial([intensity](CoreEngine::MaterialInstance* mat) {
        mat->SetIBLIntensity(intensity);
    });
}

