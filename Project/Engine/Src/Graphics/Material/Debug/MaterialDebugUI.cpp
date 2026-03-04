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

        float shininess = mat->GetShininess();
        if (ImGui::SliderFloat("Shininess", &shininess, 1.0f, 128.0f)) {
            mat->SetShininess(shininess);
            changed = true;
        }

        // ─────────────── Lighting ───────────────
        ImGui::SeparatorText("Lighting");

        bool enableLighting = mat->IsLightingEnabled();
        if (ImGui::Checkbox("Enable Lighting", &enableLighting)) {
            mat->SetLightingEnabled(enableLighting);
            changed = true;
        }

        if (enableLighting) {
            const char* shadingModes[] = { "None", "Lambert", "Half-Lambert", "Toon" };
            int shadingModeInt = static_cast<int>(mat->GetShadingMode());

            if (ImGui::Combo("Shading Mode", &shadingModeInt, shadingModes, 4)) {
                mat->SetShadingMode(static_cast<ShadingMode>(shadingModeInt));
                changed = true;
            }

            if (mat->GetShadingMode() == ShadingMode::Toon) {
                ImGui::Indent();
                float toonThreshold = mat->GetToonThreshold();
                if (ImGui::SliderFloat("Threshold##Toon", &toonThreshold, 0.0f, 1.0f)) {
                    mat->SetToonThreshold(toonThreshold);
                    changed = true;
                }
                float toonSmoothness = mat->GetToonSmoothness();
                if (ImGui::SliderFloat("Smoothness##Toon", &toonSmoothness, 0.0f, 0.5f)) {
                    mat->SetToonSmoothness(toonSmoothness);
                    changed = true;
                }
                ImGui::Unindent();
            }
        }

        // ─────────────── PBR ───────────────
        ImGui::SeparatorText("PBR");

        bool enablePBR = mat->IsPBREnabled();
        if (ImGui::Checkbox("Enable PBR", &enablePBR)) {
            mat->SetPBREnabled(enablePBR);
            changed = true;
        }

        if (enablePBR) {
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

            ImGui::TextDisabled("Parameters (when maps disabled)");

            // テクスチャが存在しないか、マップが無効な場合のみスライダーを有効化
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
        }

        // ─────────────── Environment ───────────────
        ImGui::SeparatorText("Environment");

        const bool envMapAvailable = Model::IsEnvironmentMapAvailable();
        ImGui::BeginDisabled(!envMapAvailable);
        bool enableEnvMap = mat->IsEnvironmentMapEnabled();
        if (ImGui::Checkbox("Enable Environment Map", &enableEnvMap)) {
            mat->SetEnvironmentMapEnabled(enableEnvMap);
            changed = true;
        }
        ImGui::EndDisabled();
        if (!envMapAvailable) {
            ImGui::SameLine();
            ImGui::TextDisabled("(テクスチャ未設定)");
            if (mat->IsEnvironmentMapEnabled()) {
                mat->SetEnvironmentMapEnabled(false);
            }
        }
        if (enableEnvMap && envMapAvailable) {
            ImGui::Indent();
            float intensity = mat->GetEnvironmentMapIntensity();
            if (ImGui::SliderFloat("Intensity##EnvMap", &intensity, 0.0f, 1.0f)) {
                mat->SetEnvironmentMapIntensity(intensity);
                changed = true;
            }
            float rotationDeg = mat->GetEnvironmentRotationY() * 57.2958f;
            if (ImGui::SliderFloat("Rotation Y (deg)", &rotationDeg, 0.0f, 360.0f)) {
                mat->SetEnvironmentRotationY(rotationDeg / 57.2958f);
                changed = true;
            }
            ImGui::Unindent();
        }

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
