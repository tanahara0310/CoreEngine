#include "pch.h"
#include "MaterialComponent.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

#include "GameObject/Component/Render/MeshRendererComponent.h"

namespace CoreEngine
{
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

#ifdef USE_IMGUI
    bool MaterialComponent::DrawInspector()
    {
        MaterialInstance* material = GetMaterial();
        if (!material) {
            // モデルがまだ読めていないと実体が無い。値を持たないので編集もできない
            UI::Hint("マテリアル未生成（メッシュの読み込み待ち）");
            return false;
        }

        bool changed = false;

        Vector4 color = material->GetColor();
        if (UI::ColorEdit("ベースカラー", color)) {
            SetColor(color);
            changed = true;
        }

        UI::SectionHeader("PBR");

        float metallic = material->GetMetallic();
        float roughness = material->GetRoughness();
        float occlusion = material->GetOcclusionStrength();

        bool pbrChanged = false;
        pbrChanged |= UI::SliderFloat("メタリック", metallic, 0.0f, 1.0f);
        pbrChanged |= UI::SliderFloat("ラフネス", roughness, 0.0f, 1.0f);
        pbrChanged |= UI::SliderFloat("オクルージョン", occlusion, 0.0f, 1.0f);
        if (pbrChanged) {
            SetPBR(metallic, roughness, occlusion);
            changed = true;
        }

        UI::SectionHeader("その他");

        float iblIntensity = material->GetIBLIntensity();
        if (UI::SliderFloat("IBL 強度", iblIntensity, 0.0f, 2.0f)) {
            SetIBLIntensity(iblIntensity);
            changed = true;
        }

        bool lighting = material->IsLightingEnabled();
        if (ImGui::Checkbox("ライティング", &lighting)) {
            SetLightingEnabled(lighting);
            changed = true;
        }

        bool normalMap = material->IsNormalMapEnabled();
        if (ImGui::Checkbox("法線マップ", &normalMap)) {
            SetNormalMapEnabled(normalMap);
            changed = true;
        }

        return changed;
    }
#endif // USE_IMGUI
}
