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

void ModelObject::SetPBRParameters(float metallic, float roughness, float ao) {
    if (auto* mat = GetMaterial()) {
        mat->SetMetallic(metallic);
        mat->SetRoughness(roughness);
        mat->SetAO(ao);
    }
}

void ModelObject::SetPBRTextureMapsEnabled(bool useNormal, bool useMetallic,
    bool useRoughness, bool useAO) {
    if (auto* mat = GetMaterial()) {
        mat->SetNormalMapEnabled(useNormal);
        mat->SetMetallicMapEnabled(useMetallic);
        mat->SetRoughnessMapEnabled(useRoughness);
        mat->SetAOMapEnabled(useAO);
    }
}

void ModelObject::SetMaterialColor(const CoreEngine::Vector4& color) {
    if (auto* mat = GetMaterial()) mat->SetColor(color);
}

void ModelObject::SetIBLEnabled(bool enable) {
    if (auto* mat = GetMaterial()) mat->SetIBLEnabled(enable);
}

void ModelObject::SetIBLIntensity(float intensity) {
    if (auto* mat = GetMaterial()) mat->SetIBLIntensity(intensity);
}

