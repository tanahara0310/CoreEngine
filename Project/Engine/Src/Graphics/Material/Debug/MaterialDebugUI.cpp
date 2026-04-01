#include "MaterialDebugUI.h"

#ifdef _DEBUG

#include "Graphics/Model/Model.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Utility/Debug/ImGui/ImGuiAll.h"

namespace CoreEngine {

    bool MaterialDebugUI::Draw(Model* model) {
        if (!model) return false;

        auto* mat = model->GetMaterial();
        if (!mat) return false;

        bool changed = false;

        UI::Scope::TreeScope matTree("Material");
        if (!matTree) return false;

        // ─────────────── Base ───────────────
        UI::SectionHeader("Base");

        Vector4 color = mat->GetColor();
        if (UI::ColorEdit("Color", color)) {
            mat->SetColor(color);
            changed = true;
        }

        // ─────────────── Lighting ───────────────
        UI::SectionHeader("Lighting");

        bool enableLighting = mat->IsLightingEnabled();
        if (UI::Widgets::ToggleSwitch("Enable Lighting (PBR)", &enableLighting)) {
            mat->SetLightingEnabled(enableLighting);
            changed = true;
        }
        UI::SameLine();
        UI::Hint(enableLighting ? "PBR" : "Unlit");

        // ─────────────── PBR Parameters ───────────────
        UI::SectionHeader("PBR Parameters");

        if (auto mapsTree = UI::Scope::TreeScope("Texture Maps##PBR")) {
            const bool hasNormal    = model->HasNormalMap();
            const bool hasMetallic  = model->HasMetallicRoughnessMap();
            const bool hasOcclusion = model->HasOcclusionMap();

            auto drawMapToggle = [&](const char* label, bool hasTexture,
                bool (MaterialInstance::*getter)() const,
                void (MaterialInstance::*setter)(bool)) {
                {
                    UI::Scope::DisabledScope ds(!hasTexture);
                    bool val = (mat->*getter)();
                    if (UI::Widgets::ToggleSwitch(label, &val)) {
                        (mat->*setter)(val);
                        changed = true;
                    }
                }
                if (!hasTexture) {
                    UI::SameLine();
                    UI::Hint("(なし)");
                }
            };

            drawMapToggle("Normal Map",    hasNormal,    &MaterialInstance::IsNormalMapEnabled,    &MaterialInstance::SetNormalMapEnabled);
            drawMapToggle("Metallic Map",  hasMetallic,  &MaterialInstance::IsMetallicMapEnabled,  &MaterialInstance::SetMetallicMapEnabled);
            drawMapToggle("Roughness Map", hasMetallic,  &MaterialInstance::IsRoughnessMapEnabled, &MaterialInstance::SetRoughnessMapEnabled);
            drawMapToggle("AO Map",        hasOcclusion, &MaterialInstance::IsAOMapEnabled,        &MaterialInstance::SetAOMapEnabled);
        }

        UI::Hint("Parameters (maps disabled 時に有効)");

        {
            UI::Scope::DisabledScope ds(mat->IsMetallicMapEnabled() && model->HasMetallicRoughnessMap());
            float metallic = mat->GetMetallic();
            if (UI::SliderFloat("Metallic", metallic, 0.0f, 1.0f)) {
                mat->SetMetallic(metallic);
                changed = true;
            }
        }
        {
            UI::Scope::DisabledScope ds(mat->IsRoughnessMapEnabled() && model->HasMetallicRoughnessMap());
            float roughness = mat->GetRoughness();
            if (UI::SliderFloat("Roughness", roughness, 0.0f, 1.0f)) {
                mat->SetRoughness(roughness);
                changed = true;
            }
        }
        {
            UI::Scope::DisabledScope ds(mat->IsAOMapEnabled() && model->HasOcclusionMap());
            float ao = mat->GetAO();
            if (UI::SliderFloat("AO", ao, 0.0f, 1.0f)) {
                mat->SetAO(ao);
                changed = true;
            }
        }

        // ─────────────── IBL ───────────────
        UI::SectionHeader("IBL");

        const bool iblAvailable = model->IsIBLAvailable();
        bool enableIBL = mat->IsIBLEnabled();
        {
            UI::Scope::DisabledScope ds(!iblAvailable);
            if (UI::Widgets::ToggleSwitch("Enable IBL", &enableIBL)) {
                mat->SetIBLEnabled(enableIBL);
                changed = true;
            }
        }
        if (!iblAvailable) {
            UI::SameLine();
            UI::Hint("(Irradiance/Prefiltered/BRDF LUT 未設定)");
            if (mat->IsIBLEnabled()) {
                mat->SetIBLEnabled(false);
            }
        }
        if (enableIBL && iblAvailable) {
            UI::Scope::IndentScope is;
            float iblIntensity = mat->GetIBLIntensity();
            if (UI::SliderFloat("IBL Intensity", iblIntensity, 0.0f, 2.0f)) {
                mat->SetIBLIntensity(iblIntensity);
                changed = true;
            }
            UI::Hint("Env Rotation Y: scene-level (SkyBox)");
        }

        // ─────────────── Effects ───────────────
        UI::SectionHeader("Effects");

        bool enableDithering = mat->IsDitheringEnabled();
        if (UI::Widgets::ToggleSwitch("Enable Dithering", &enableDithering)) {
            mat->SetDitheringEnabled(enableDithering);
            changed = true;
        }
        if (enableDithering) {
            UI::Scope::IndentScope is;
            float ditheringScale = mat->GetDitheringScale();
            if (UI::SliderFloat("Scale##Dithering", ditheringScale, 0.1f, 5.0f)) {
                mat->SetDitheringScale(ditheringScale);
                changed = true;
            }
        }

        return changed;
    }

} // namespace CoreEngine

#endif
