#include "MaterialDebugUI.h"

#ifdef _DEBUG

#include "Graphics/Model/Model.h"
#include "Graphics/Material/MaterialConstants.h"
#include "externals/imgui/imgui.h"

namespace CoreEngine {

    bool MaterialDebugUI::Draw(Model* model) {
        if (!model) return false;

        auto* mat = model->GetMaterial();
        if (!mat) return false;

        bool changed = false;

        if (!ImGui::TreeNode("Material")) {
            return false;
        }

        // ─────────────── Base ───────────────
        ImGui::SeparatorText("Base");

        Vector4 color = mat->GetColor();
        if (ImGui::ColorEdit4("Color", &color.x)) {
            mat->SetColor(color);
            changed = true;
        }

        // ─────────────── Lighting ───────────────
        ImGui::SeparatorText("Lighting");

        bool enableLighting = mat->IsLightingEnabled();
        if (ImGui::Checkbox("Enable Lighting (PBR)", &enableLighting)) {
            mat->SetLightingEnabled(enableLighting);
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(enableLighting ? "PBR" : "Unlit");

        // ─────────────── PBR Parameters ───────────────
        ImGui::SeparatorText("PBR Parameters");

        if (ImGui::TreeNode("Texture Maps##PBR")) {
            const bool hasNormal    = model->HasNormalMap();
            const bool hasMetallic  = model->HasMetallicRoughnessMap();
            const bool hasOcclusion = model->HasOcclusionMap();

            auto drawMapCheckbox = [&](const char* label, bool hasTexture,
                bool (MaterialInstance::*getter)() const,
                void (MaterialInstance::*setter)(bool)) {
                ImGui::BeginDisabled(!hasTexture);
                bool val = (mat->*getter)();
                if (ImGui::Checkbox(label, &val)) {
                    (mat->*setter)(val);
                    changed = true;
                }
                ImGui::EndDisabled();
                if (!hasTexture) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(なし)");
                }
            };

            drawMapCheckbox("Normal Map",    hasNormal,    &MaterialInstance::IsNormalMapEnabled,    &MaterialInstance::SetNormalMapEnabled);
            drawMapCheckbox("Metallic Map",  hasMetallic,  &MaterialInstance::IsMetallicMapEnabled,  &MaterialInstance::SetMetallicMapEnabled);
            drawMapCheckbox("Roughness Map", hasMetallic,  &MaterialInstance::IsRoughnessMapEnabled, &MaterialInstance::SetRoughnessMapEnabled);
            drawMapCheckbox("AO Map",        hasOcclusion, &MaterialInstance::IsAOMapEnabled,        &MaterialInstance::SetAOMapEnabled);

            ImGui::TreePop();
        }

        ImGui::TextDisabled("Parameters (maps disabled 時に有効)");

        ImGui::BeginDisabled(mat->IsMetallicMapEnabled() && model->HasMetallicRoughnessMap());
        float metallic = mat->GetMetallic();
        if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
            mat->SetMetallic(metallic);
            changed = true;
        }
        ImGui::EndDisabled();

            ImGui::BeginDisabled(mat->IsRoughnessMapEnabled() && model->HasMetallicRoughnessMap());
        float roughness = mat->GetRoughness();
        if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
            mat->SetRoughness(roughness);
            changed = true;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(mat->IsAOMapEnabled() && model->HasOcclusionMap());
        float ao = mat->GetAO();
        if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f)) {
            mat->SetAO(ao);
            changed = true;
        }
        ImGui::EndDisabled();

        // ─────────────── IBL ───────────────
        ImGui::SeparatorText("IBL");

        const bool iblAvailable = Model::IsIBLAvailable();
        ImGui::BeginDisabled(!iblAvailable);
        bool enableIBL = mat->IsIBLEnabled();
        if (ImGui::Checkbox("Enable IBL", &enableIBL)) {
            mat->SetIBLEnabled(enableIBL);
            changed = true;
        }
        ImGui::EndDisabled();
        if (!iblAvailable) {
            ImGui::SameLine();
            ImGui::TextDisabled("(Irradiance/Prefiltered/BRDF LUT 未設定)");
            if (mat->IsIBLEnabled()) {
                mat->SetIBLEnabled(false);
            }
        }
        if (enableIBL && iblAvailable) {
            ImGui::Indent();
            float iblIntensity = mat->GetIBLIntensity();
            if (ImGui::SliderFloat("IBL Intensity", &iblIntensity, 0.0f, 2.0f)) {
                mat->SetIBLIntensity(iblIntensity);
                changed = true;
            }
            ImGui::TextDisabled("Env Rotation Y: scene-level (SkyBox)");
            ImGui::Unindent();
        }

        // ─────────────── Effects ───────────────
        ImGui::SeparatorText("Effects");

        bool enableDithering = mat->IsDitheringEnabled();
        if (ImGui::Checkbox("Enable Dithering", &enableDithering)) {
            mat->SetDitheringEnabled(enableDithering);
            changed = true;
        }
        if (enableDithering) {
            ImGui::Indent();
            float ditheringScale = mat->GetDitheringScale();
            if (ImGui::SliderFloat("Scale##Dithering", &ditheringScale, 0.1f, 5.0f)) {
                mat->SetDitheringScale(ditheringScale);
                changed = true;
            }
            ImGui::Unindent();
        }

        ImGui::TreePop();
        return changed;
    }

} // namespace CoreEngine

#endif
