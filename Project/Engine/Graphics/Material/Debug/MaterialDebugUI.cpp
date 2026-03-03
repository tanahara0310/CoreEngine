#include "MaterialDebugUI.h"

#ifdef _DEBUG

#include "Engine/Graphics/Model/Model.h"
#include "Engine/Graphics/Structs/MaterialConstants.h"
#include "externals/imgui/imgui.h"

namespace CoreEngine {

    bool MaterialDebugUI::Draw(Model* model) {
        if (!model) return false;

        auto* mat = model->GetMaterial();
        if (!mat) return false;

        bool changed = false;

        if (ImGui::CollapsingHeader("Material")) {

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
        }

        if (ImGui::CollapsingHeader("Lighting")) {

            bool enableLighting = mat->IsLightingEnabled();

            if (ImGui::Checkbox("Enable Lighting", &enableLighting)) {
                mat->SetLightingEnabled(enableLighting);
                changed = true;
            }

            const char* shadingModes[] = { "None", "Lambert", "Half-Lambert", "Toon" };

            int shadingModeInt = static_cast<int>(mat->GetShadingMode());

            if (ImGui::Combo("Shading Mode", &shadingModeInt, shadingModes, 4)) {

                mat->SetShadingMode(static_cast<ShadingMode>(shadingModeInt));
                changed = true;
            }

            if (mat->GetShadingMode() == ShadingMode::Toon) {
                float toonThreshold = mat->GetToonThreshold();
                if (ImGui::SliderFloat("Toon Threshold", &toonThreshold, 0.0f, 1.0f)) { mat->SetToonThreshold(toonThreshold); changed = true; }
                float toonSmoothness = mat->GetToonSmoothness();
                if (ImGui::SliderFloat("Toon Smoothness", &toonSmoothness, 0.0f, 0.5f)) { mat->SetToonSmoothness(toonSmoothness); changed = true; }
            }
        }

        if (ImGui::CollapsingHeader("PBR")) {

            bool enablePBR = mat->IsPBREnabled();

            if (ImGui::Checkbox("Enable PBR", &enablePBR)) {
                mat->SetPBREnabled(enablePBR);
                changed = true;
            }
            if (enablePBR) {

                ImGui::Separator();
                ImGui::Text("Texture Maps");

                bool useNormalMap = mat->IsNormalMapEnabled();
                bool useMetallicMap = mat->IsMetallicMapEnabled();
                bool useRoughnessMap = mat->IsRoughnessMapEnabled();
                bool useAOMap = mat->IsAOMapEnabled();

                if (ImGui::Checkbox("Normal Map", &useNormalMap)) {
                    mat->SetNormalMapEnabled(useNormalMap);
                    changed = true;
                }

                if (ImGui::Checkbox("Metallic Map", &useMetallicMap)) {
                    mat->SetMetallicMapEnabled(useMetallicMap);
                    changed = true;
                }

                if (ImGui::Checkbox("Roughness Map", &useRoughnessMap)) {
                    mat->SetRoughnessMapEnabled(useRoughnessMap);
                    changed = true;
                }

                if (ImGui::Checkbox("AO Map", &useAOMap)) {

                    mat->SetAOMapEnabled(useAOMap);

                    changed = true;
                }

                ImGui::Separator();

                ImGui::Text("Parameters (used when maps are disabled)");
                float metallic = mat->GetMetallic();

                if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f)) {
                    mat->SetMetallic(metallic);
                    changed = true;
                }

                float roughness = mat->GetRoughness();
                if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f)) {
                    mat->SetRoughness(roughness);
                    changed = true;
                }
                float ao = mat->GetAO();
                if (ImGui::SliderFloat("AO", &ao, 0.0f, 1.0f)) {
                    mat->SetAO(ao);
                    changed = true;
                }
            }
        }

        if (ImGui::CollapsingHeader("Environment Map")) {
            bool enableEnvMap = mat->IsEnvironmentMapEnabled();
            if (ImGui::Checkbox("Enable Environment Map", &enableEnvMap)) { mat->SetEnvironmentMapEnabled(enableEnvMap); changed = true; }
            if (enableEnvMap) {
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
            }
        }

        if (ImGui::CollapsingHeader("IBL (Image-Based Lighting)")) {
            bool enableIBL = mat->IsIBLEnabled();
            if (ImGui::Checkbox("Enable IBL", &enableIBL)) {
                mat->SetIBLEnabled(enableIBL); changed = true;
            }

            if (enableIBL) {

                float iblIntensity = mat->GetIBLIntensity();
                if (ImGui::SliderFloat("IBL Intensity", &iblIntensity, 0.0f, 2.0f)) {
                    mat->SetIBLIntensity(iblIntensity); changed = true;
                }
            }
        }

        if (ImGui::CollapsingHeader("Dithering")) {
            bool enableDithering = mat->IsDitheringEnabled();
            if (ImGui::Checkbox("Enable Dithering", &enableDithering)) {
                mat->SetDitheringEnabled(enableDithering); changed = true;
            }
            if (enableDithering) {
                float ditheringScale = mat->GetDitheringScale();
                if (ImGui::SliderFloat("Dithering Scale", &ditheringScale, 0.1f, 5.0f)) {
                    mat->SetDitheringScale(ditheringScale); changed = true;
                }
            }
        }

        return changed;
    }

} // namespace CoreEngine

#endif
