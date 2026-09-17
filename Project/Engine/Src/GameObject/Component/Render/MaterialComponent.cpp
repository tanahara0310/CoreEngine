#include "pch.h"
#include "MaterialComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"

REFLECT_REGISTER(CoreEngine::MaterialComponent)
COMPONENT_REGISTER(CoreEngine::MaterialComponent)

namespace CoreEngine
{
    bool MaterialComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const MeshRendererComponent*>(&other) != nullptr;
    }

    void MaterialComponent::Start()
    {
        renderer_ = Sibling<MeshRendererComponent>();

        // Start より前（AddComponent 直後）に指定された値をここで反映する
        if (pendingPBR_) {
            SetPBR(pendingPBR_->metallic, pendingPBR_->roughness, pendingPBR_->occlusion);
            pendingPBR_.reset();
        }
        if (pendingColor_) { SetColor(*pendingColor_); pendingColor_.reset(); }
        if (pendingNormalMap_) { SetNormalMapEnabled(*pendingNormalMap_); pendingNormalMap_.reset(); }
        if (pendingIBL_) { SetIBLIntensity(*pendingIBL_); pendingIBL_.reset(); }
        if (pendingLighting_) { SetLightingEnabled(*pendingLighting_); pendingLighting_.reset(); }
        if (pendingEmissive_) {
            const Vector3 emissive = *pendingEmissive_;
            pendingEmissive_.reset();
            SetEmissive({ emissive.x, emissive.y, emissive.z, 1.0f });
        }
        if (pendingDitheringScale_) { SetDitheringScale(*pendingDitheringScale_); pendingDitheringScale_.reset(); }
        if (pendingDithering_) { SetDitheringEnabled(*pendingDithering_); pendingDithering_.reset(); }
    }

    bool MaterialComponent::ForEachMaterial(const std::function<void(MaterialInstance*)>& fn) const
    {
        if (!renderer_) {
            renderer_ = Sibling<MeshRendererComponent>();
        }
        Model* model = renderer_ ? renderer_->GetModel() : nullptr;
        if (!model) {
            return false;
        }
        model->ForEachMaterial(fn);
        return true;
    }

    MaterialInstance* MaterialComponent::GetMaterial() const
    {
        if (!renderer_) {
            renderer_ = Sibling<MeshRendererComponent>();
        }
        Model* model = renderer_ ? renderer_->GetModel() : nullptr;
        return model ? model->GetMaterial() : nullptr;
    }

    void MaterialComponent::UpdateBlendModeForAlpha() const
    {
        if (!renderer_) { return; }
        const MaterialInstance* mat = GetMaterial();
        if (!mat) { return; }

        // 加算などを選んでいるときは変えない
        const BlendMode current = renderer_->GetBlendMode();
        if (current != BlendMode::kBlendModeNone && current != BlendMode::kBlendModeNormal) {
            return;
        }

        // α < 1 かつディザリング OFF → アルファブレンドで段階的透明。
        // ディザリング ON なら不透明のままディザに任せる。
        const bool needsAlphaBlend = mat->GetColor().w < 1.0f && !mat->IsDitheringEnabled();
        renderer_->SetBlendMode(needsAlphaBlend
            ? BlendMode::kBlendModeNormal
            : BlendMode::kBlendModeNone);
    }

    void MaterialComponent::SetPBR(float metallic, float roughness, float occlusionStrength)
    {
        const bool applied = ForEachMaterial([&](MaterialInstance* mat) {
            mat->SetMetallic(metallic);
            mat->SetRoughness(roughness);
            mat->SetOcclusionStrength(occlusionStrength);
            });
        if (!applied) {
            pendingPBR_ = PendingPBR{ metallic, roughness, occlusionStrength };
        }
    }

    void MaterialComponent::SetColor(const Vector4& color)
    {
        const bool applied = ForEachMaterial([&color](MaterialInstance* mat) {
            mat->SetColor(color);
            });
        if (applied) {
            UpdateBlendModeForAlpha();
        } else {
            pendingColor_ = color;
        }
    }

    void MaterialComponent::SetNormalMapEnabled(bool enable)
    {
        if (!ForEachMaterial([enable](MaterialInstance* mat) { mat->SetNormalMapEnabled(enable); })) {
            pendingNormalMap_ = enable;
        }
    }

    void MaterialComponent::SetIBLIntensity(float intensity)
    {
        if (!ForEachMaterial([intensity](MaterialInstance* mat) { mat->SetIBLIntensity(intensity); })) {
            pendingIBL_ = intensity;
        }
    }

    void MaterialComponent::SetLightingEnabled(bool enable)
    {
        if (!ForEachMaterial([enable](MaterialInstance* mat) { mat->SetLightingEnabled(enable); })) {
            pendingLighting_ = enable;
        }
    }

    void MaterialComponent::SetMetallic(float value)
    {
        if (!ForEachMaterial([value](MaterialInstance* mat) { mat->SetMetallic(value); })) {
            SetPBR(value, GetRoughness(), GetOcclusionStrength());
        }
    }

    void MaterialComponent::SetRoughness(float value)
    {
        if (!ForEachMaterial([value](MaterialInstance* mat) { mat->SetRoughness(value); })) {
            SetPBR(GetMetallic(), value, GetOcclusionStrength());
        }
    }

    void MaterialComponent::SetOcclusionStrength(float value)
    {
        if (!ForEachMaterial([value](MaterialInstance* mat) { mat->SetOcclusionStrength(value); })) {
            SetPBR(GetMetallic(), GetRoughness(), value);
        }
    }

    void MaterialComponent::SetEmissive(const Vector4& color)
    {
        const Vector3 emissive = { color.x, color.y, color.z };
        if (!ForEachMaterial([&emissive](MaterialInstance* mat) { mat->SetEmissiveFactor(emissive); })) {
            pendingEmissive_ = emissive;
        }
    }

    void MaterialComponent::SetDitheringEnabled(bool enable)
    {
        const bool applied = ForEachMaterial([enable](MaterialInstance* mat) {
            mat->SetDitheringEnabled(enable);
            });
        if (applied) {
            UpdateBlendModeForAlpha();
        } else {
            pendingDithering_ = enable;
        }
    }

    void MaterialComponent::SetDitheringScale(float scale)
    {
        if (!ForEachMaterial([scale](MaterialInstance* mat) { mat->SetDitheringScale(scale); })) {
            pendingDitheringScale_ = scale;
        }
    }

    // ===== 取得 =====
    // 実体（MaterialInstance）が出来るのはメッシュを読み終えた後なので、
    // それまでは Start で流し込む控えを返す。どちらも無ければエンジン既定値

    Vector4 MaterialComponent::GetColor() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->GetColor(); }
        return pendingColor_.value_or(Vector4{ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    float MaterialComponent::GetMetallic() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->GetMetallic(); }
        return pendingPBR_ ? pendingPBR_->metallic : 0.0f;
    }

    float MaterialComponent::GetRoughness() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->GetRoughness(); }
        return pendingPBR_ ? pendingPBR_->roughness : 1.0f;
    }

    float MaterialComponent::GetOcclusionStrength() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->GetOcclusionStrength(); }
        return pendingPBR_ ? pendingPBR_->occlusion : 1.0f;
    }

    float MaterialComponent::GetIBLIntensity() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->GetIBLIntensity(); }
        return pendingIBL_.value_or(1.0f);
    }

    bool MaterialComponent::IsLightingEnabled() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->IsLightingEnabled(); }
        return pendingLighting_.value_or(true);
    }

    bool MaterialComponent::IsNormalMapEnabled() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->IsNormalMapEnabled(); }
        return pendingNormalMap_.value_or(false);
    }

    Vector4 MaterialComponent::GetEmissive() const
    {
        const Vector3 emissive = GetMaterial()
            ? GetMaterial()->GetEmissiveFactor()
            : pendingEmissive_.value_or(Vector3{ 0.0f, 0.0f, 0.0f });
        return { emissive.x, emissive.y, emissive.z, 1.0f };
    }

    bool MaterialComponent::IsDitheringEnabled() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->IsDitheringEnabled(); }
        return pendingDithering_.value_or(true);
    }

    float MaterialComponent::GetDitheringScale() const
    {
        if (const MaterialInstance* mat = GetMaterial()) { return mat->GetDitheringScale(); }
        return pendingDitheringScale_.value_or(1.0f);
    }
}
