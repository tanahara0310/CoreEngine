#include "pch.h"
#include "LightDebugVisualizer.h"

#include "Math/MathCore.h"
#include "Graphics/Line/LineManager.h"
#include <cmath>

#ifdef _DEBUG
#include "Utility/Debug/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    void LightDebugVisualizer::DrawImGui(
        std::vector<DirectionalLightData>& directionalLights,
        std::vector<PointLightData>& pointLights,
        std::vector<SpotLightData>& spotLights,
        std::vector<AreaLightData>& areaLights,
        uint32_t maxDirectionalLights,
        uint32_t maxPointLights,
        uint32_t maxSpotLights,
        uint32_t maxAreaLights,
        const std::function<void()>& onAddDirectionalLight,
        const std::function<void()>& onAddPointLight,
        const std::function<void()>& onAddSpotLight,
        const std::function<void()>& onAddAreaLight
    )
    {
#ifdef _DEBUG
        constexpr float kPi = 3.14159265f;

        // ── 概要 ──
        {
            uint32_t total = static_cast<uint32_t>(
                directionalLights.size() + pointLights.size() +
                spotLights.size() + areaLights.size());
            uint32_t totalMax = maxDirectionalLights + maxPointLights +
                maxSpotLights + maxAreaLights;

            float fraction = (totalMax > 0) ? static_cast<float>(total) / totalMax : 0.0f;
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%u / %u", total, totalMax);
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);

            UI::Widgets::ToggleSwitch("デバッグ可視化", &enableVisualization_);

            auto setAllEnabled = [&](bool enabled) {
                for (auto& l : directionalLights) l.enabled = enabled;
                for (auto& l : pointLights)       l.enabled = enabled;
                for (auto& l : spotLights)        l.enabled = enabled;
                for (auto& l : areaLights)        l.enabled = enabled;
            };
            if (ImGui::SmallButton("全て有効")) setAllEnabled(true);
            UI::SameLine();
            if (ImGui::SmallButton("全て無効")) setAllEnabled(false);
        }

        UI::Spacing();

        // ── 操作ボタンヘルパー ──
        auto drawLightActions = [](int index, int& deleteIdx, int& dupIdx) {
            UI::Spacing();
            UI::Separator();
            if (ImGui::SmallButton("複製")) dupIdx = index;
            UI::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::SmallButton("削除")) deleteIdx = index;
            ImGui::PopStyleColor(2);
        };

                    // ═══ Directional Light ═══
                    {
                        UI::SectionHeader("Directional Light");
                        UI::HintF("  %u / %u", static_cast<uint32_t>(directionalLights.size()), maxDirectionalLights);

                        int deleteIdx = -1, dupIdx = -1;

                        for (size_t i = 0; i < directionalLights.size(); ++i) {
                            auto& light = directionalLights[i];
                            ImGui::PushID(static_cast<int>(i));

                            ImGui::ColorButton("##clr", ImVec4(light.color.x, light.color.y, light.color.z, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                            UI::SameLine();
                            UI::Widgets::ToggleSwitch("##en", &light.enabled);
                            UI::SameLine();

                            char label[64];
                            snprintf(label, sizeof(label), "Directional #%d", static_cast<int>(i));
                            if (ImGui::TreeNode(label)) {
                                UI::ColorEdit("色", light.color);
                                UI::DragVec3("方向", light.direction, 0.01f, -1.0f, 1.0f);
                                UI::DragFloat("強度", light.intensity, 0.01f, 0.0f, 10.0f);

                                if (ImGui::SmallButton("方向を正規化")) {
                                    light.direction = MathCore::Vector::Normalize(light.direction);
                                }
                                drawLightActions(static_cast<int>(i), deleteIdx, dupIdx);
                                ImGui::TreePop();
                            }

                            ImGui::PopID();
                        }

                        if (deleteIdx >= 0) {
                            directionalLights.erase(directionalLights.begin() + deleteIdx);
                        } else if (dupIdx >= 0 && directionalLights.size() < maxDirectionalLights) {
                            directionalLights.push_back(directionalLights[dupIdx]);
                        }

                        UI::Spacing();
                        if (directionalLights.size() < maxDirectionalLights) {
                            if (ImGui::Button("+ Directional Light")) {
                                if (onAddDirectionalLight) onAddDirectionalLight();
                            }
                        } else {
                            UI::Hint("最大数に達しています");
                        }
                    }

                    UI::Spacing();

                    // ═══ Point Light ═══
                    {
                        UI::SectionHeader("Point Light");
                        UI::HintF("  %u / %u", static_cast<uint32_t>(pointLights.size()), maxPointLights);

                        int deleteIdx = -1, dupIdx = -1;

                        for (size_t i = 0; i < pointLights.size(); ++i) {
                            auto& light = pointLights[i];
                            ImGui::PushID(static_cast<int>(i + 1000));

                            ImGui::ColorButton("##clr", ImVec4(light.color.x, light.color.y, light.color.z, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                            UI::SameLine();
                            UI::Widgets::ToggleSwitch("##en", &light.enabled);
                            UI::SameLine();

                            char label[64];
                            snprintf(label, sizeof(label), "Point #%d", static_cast<int>(i));
                            if (ImGui::TreeNode(label)) {
                                UI::ColorEdit("色", light.color);
                                UI::DragVec3("位置", light.position, 0.1f, -50.0f, 50.0f);
                                UI::DragFloat("強度", light.intensity, 0.01f, 0.0f, 10.0f);
                                UI::DragFloat("半径", light.radius, 0.1f, 0.1f, 100.0f);
                                UI::DragFloat("減衰率", light.decay, 0.01f, 0.0f, 10.0f);

                                drawLightActions(static_cast<int>(i), deleteIdx, dupIdx);
                                ImGui::TreePop();
                            }

                            ImGui::PopID();
                        }

                        if (deleteIdx >= 0) {
                            pointLights.erase(pointLights.begin() + deleteIdx);
                        } else if (dupIdx >= 0 && pointLights.size() < maxPointLights) {
                            pointLights.push_back(pointLights[dupIdx]);
                        }

                        UI::Spacing();
                        if (pointLights.size() < maxPointLights) {
                            if (ImGui::Button("+ Point Light")) {
                                if (onAddPointLight) onAddPointLight();
                            }
                        } else {
                            UI::Hint("最大数に達しています");
                        }
                    }

                    UI::Spacing();

                    // ═══ Spot Light ═══
                    {
                        UI::SectionHeader("Spot Light");
                        UI::HintF("  %u / %u", static_cast<uint32_t>(spotLights.size()), maxSpotLights);

                        int deleteIdx = -1, dupIdx = -1;

                        for (size_t i = 0; i < spotLights.size(); ++i) {
                            auto& light = spotLights[i];
                            ImGui::PushID(static_cast<int>(i + 2000));

                            ImGui::ColorButton("##clr", ImVec4(light.color.x, light.color.y, light.color.z, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                            UI::SameLine();
                            UI::Widgets::ToggleSwitch("##en", &light.enabled);
                            UI::SameLine();

                            char label[64];
                            snprintf(label, sizeof(label), "Spot #%d", static_cast<int>(i));
                            if (ImGui::TreeNode(label)) {
                                UI::ColorEdit("色", light.color);
                                UI::DragVec3("位置", light.position, 0.1f, -50.0f, 50.0f);
                                UI::DragVec3("方向", light.direction, 0.01f, -1.0f, 1.0f);
                                UI::DragFloat("強度", light.intensity, 0.01f, 0.0f, 10.0f);
                                UI::DragFloat("距離", light.distance, 0.1f, 0.1f, 100.0f);
                                UI::DragFloat("減衰率", light.decay, 0.01f, 0.0f, 10.0f);

                                float cosAngleClamped = std::max(-1.0f, std::min(1.0f, light.cosAngle));
                                float angleDeg = std::acos(cosAngleClamped) * (180.0f / kPi);
                                if (UI::SliderFloat("角度 (°)", angleDeg, 0.0f, 90.0f)) {
                                    light.cosAngle = std::cos(angleDeg * (kPi / 180.0f));
                                }

                                float cosFalloffClamped = std::max(-1.0f, std::min(1.0f, light.cosFalloffStart));
                                float falloffDeg = std::acos(cosFalloffClamped) * (180.0f / kPi);
                                if (UI::SliderFloat("フォールオフ開始 (°)", falloffDeg, 0.0f, 90.0f)) {
                                    light.cosFalloffStart = std::cos(falloffDeg * (kPi / 180.0f));
                                }

                                if (ImGui::SmallButton("方向を正規化")) {
                                    light.direction = MathCore::Vector::Normalize(light.direction);
                                }
                                drawLightActions(static_cast<int>(i), deleteIdx, dupIdx);
                                ImGui::TreePop();
                            }

                            ImGui::PopID();
                        }

                        if (deleteIdx >= 0) {
                            spotLights.erase(spotLights.begin() + deleteIdx);
                        } else if (dupIdx >= 0 && spotLights.size() < maxSpotLights) {
                            spotLights.push_back(spotLights[dupIdx]);
                        }

                        UI::Spacing();
                        if (spotLights.size() < maxSpotLights) {
                            if (ImGui::Button("+ Spot Light")) {
                                if (onAddSpotLight) onAddSpotLight();
                            }
                        } else {
                            UI::Hint("最大数に達しています");
                        }
                    }

                    UI::Spacing();

                    // ═══ Area Light ═══
                    {
                        UI::SectionHeader("Area Light");
                        UI::HintF("  %u / %u", static_cast<uint32_t>(areaLights.size()), maxAreaLights);

                        int deleteIdx = -1, dupIdx = -1;

                        for (size_t i = 0; i < areaLights.size(); ++i) {
                            auto& light = areaLights[i];
                            ImGui::PushID(static_cast<int>(i + 3000));

                            ImGui::ColorButton("##clr", ImVec4(light.color.x, light.color.y, light.color.z, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(14, 14));
                            UI::SameLine();
                            UI::Widgets::ToggleSwitch("##en", &light.enabled);
                            UI::SameLine();

                            char label[64];
                            snprintf(label, sizeof(label), "Area #%d", static_cast<int>(i));
                            if (ImGui::TreeNode(label)) {
                                UI::ColorEdit("色", light.color);
                                UI::DragVec3("位置", light.position, 0.1f, -50.0f, 50.0f);
                                UI::DragVec3("法線", light.normal, 0.01f, -1.0f, 1.0f);
                                UI::DragFloat("強度", light.intensity, 0.01f, 0.0f, 10.0f);
                                UI::DragFloat("幅", light.width, 0.1f, 0.1f, 20.0f);
                                UI::DragFloat("高さ", light.height, 0.1f, 0.1f, 20.0f);
                                UI::DragFloat("範囲", light.range, 0.1f, 0.1f, 100.0f);
                                UI::DragVec3("右ベクトル", light.right, 0.01f, -1.0f, 1.0f);
                                UI::DragVec3("上ベクトル", light.up, 0.01f, -1.0f, 1.0f);

                                if (ImGui::SmallButton("法線を正規化")) {
                                    light.normal = MathCore::Vector::Normalize(light.normal);
                                }
                                drawLightActions(static_cast<int>(i), deleteIdx, dupIdx);
                                ImGui::TreePop();
                            }

                            ImGui::PopID();
                        }

                        if (deleteIdx >= 0) {
                            areaLights.erase(areaLights.begin() + deleteIdx);
                        } else if (dupIdx >= 0 && areaLights.size() < maxAreaLights) {
                            areaLights.push_back(areaLights[dupIdx]);
                        }

                        UI::Spacing();
                        if (areaLights.size() < maxAreaLights) {
                            if (ImGui::Button("+ Area Light")) {
                                if (onAddAreaLight) onAddAreaLight();
                            }
                        } else {
                            UI::Hint("最大数に達しています");
                        }
                    }
            #else
        // Release builds - avoid unused parameter warnings
        (void)directionalLights;
        (void)pointLights;
        (void)spotLights;
        (void)areaLights;
        (void)maxDirectionalLights;
        (void)maxPointLights;
        (void)maxSpotLights;
        (void)maxAreaLights;
        (void)onAddDirectionalLight;
        (void)onAddPointLight;
        (void)onAddSpotLight;
        (void)onAddAreaLight;
#endif
    }

    void LightDebugVisualizer::DrawVisualization(
        const std::vector<DirectionalLightData>& directionalLights,
        const std::vector<PointLightData>& pointLights,
        const std::vector<SpotLightData>& spotLights
    )
    {
#ifdef _DEBUG
        if (!enableVisualization_)
        {
            return;
        }

        for (const auto& light : directionalLights)
        {
            DrawDirectionalLightVisualization(light);
        }

        for (const auto& light : pointLights)
        {
            DrawPointLightVisualization(light);
        }

        for (const auto& light : spotLights)
        {
            DrawSpotLightVisualization(light);
        }
#else
        (void)directionalLights;
        (void)pointLights;
        (void)spotLights;
#endif
    }

    void LightDebugVisualizer::DrawDirectionalLightVisualization(const DirectionalLightData& light)
    {
        (void)light;
#ifdef _DEBUG
        if (!light.enabled) return;

        auto& lineManager = LineManager::GetInstance();
        Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

        Vector3 origin = { 0.0f, 0.0f, 0.0f };
        Vector3 normalizedDir = MathCore::Vector::Normalize(light.direction);
        Vector3 directionEnd = origin + normalizedDir * 10.0f;

        lineManager.DrawLine(origin, directionEnd, lightColor, 1.0f);

        Vector3 arrowTip = directionEnd;

        Vector3 up = { 0.0f, 1.0f, 0.0f };
        if (std::abs(light.direction.y) > 0.99f)
        {
            up = { 1.0f, 0.0f, 0.0f };
        }
        Vector3 perpendicular1 = MathCore::Vector::Normalize(MathCore::Vector::Cross(light.direction, up));
        Vector3 perpendicular2 = MathCore::Vector::Normalize(MathCore::Vector::Cross(light.direction, perpendicular1));

        const int arrowFeathers = 8;
        for (int j = 0; j < arrowFeathers; ++j)
        {
            float angle = (float)j / arrowFeathers * 2.0f * 3.14159f;
            Vector3 featherDir = perpendicular1 * std::cos(angle) + perpendicular2 * std::sin(angle);
            featherDir = MathCore::Vector::Normalize(featherDir) * 0.8f;

            Vector3 featherEnd = arrowTip - normalizedDir * 1.5f + featherDir;
            lineManager.DrawLine(arrowTip, featherEnd, lightColor, 1.0f);
        }

        const int parallelRays = 5;
        float spacing = 2.0f;

        for (int x = -parallelRays / 2; x <= parallelRays / 2; ++x)
        {
            for (int z = -parallelRays / 2; z <= parallelRays / 2; ++z)
            {
                if (x == 0 && z == 0) continue;

                Vector3 offset = perpendicular1 * (float)x * spacing + perpendicular2 * (float)z * spacing;
                Vector3 rayStart = origin + offset;
                Vector3 rayEnd = rayStart + normalizedDir * 8.0f;

                lineManager.DrawLine(rayStart, rayEnd, lightColor, 0.3f);
            }
        }
#endif
    }

    void LightDebugVisualizer::DrawPointLightVisualization(const PointLightData& light)
    {
        (void)light;

#ifdef _DEBUG
        if (!light.enabled) return;

        auto& lineManager = LineManager::GetInstance();
        Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

        lineManager.DrawCross(light.position, 0.8f, lightColor, 1.0f);

        const int segments = 32;

        float effectiveRadius = light.radius;
        if (light.decay > 0.01f)
        {
            float attenuationThreshold = 0.1f;
            float normalizedDist = std::pow(attenuationThreshold, 1.0f / light.decay);
            effectiveRadius = light.radius * (1.0f - normalizedDist);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = light.position + Vector3{
                std::cos(angle1) * effectiveRadius,
                std::sin(angle1) * effectiveRadius,
                0.0f
            };
            Vector3 point2 = light.position + Vector3{
                std::cos(angle2) * effectiveRadius,
                std::sin(angle2) * effectiveRadius,
                0.0f
            };
            lineManager.DrawLine(point1, point2, lightColor, 1.0f);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = light.position + Vector3{
                std::cos(angle1) * effectiveRadius,
                0.0f,
                std::sin(angle1) * effectiveRadius
            };
            Vector3 point2 = light.position + Vector3{
                std::cos(angle2) * effectiveRadius,
                0.0f,
                std::sin(angle2) * effectiveRadius
            };
            lineManager.DrawLine(point1, point2, lightColor, 1.0f);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = light.position + Vector3{
                0.0f,
                std::cos(angle1) * effectiveRadius,
                std::sin(angle1) * effectiveRadius
            };
            Vector3 point2 = light.position + Vector3{
                0.0f,
                std::cos(angle2) * effectiveRadius,
                std::sin(angle2) * effectiveRadius
            };
            lineManager.DrawLine(point1, point2, lightColor, 1.0f);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = light.position + Vector3{
                std::cos(angle1) * light.radius,
                std::sin(angle1) * light.radius,
                0.0f
            };
            Vector3 point2 = light.position + Vector3{
                std::cos(angle2) * light.radius,
                std::sin(angle2) * light.radius,
                0.0f
            };
            lineManager.DrawLine(point1, point2, lightColor, 0.2f);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = light.position + Vector3{
                std::cos(angle1) * light.radius,
                0.0f,
                std::sin(angle1) * light.radius
            };
            Vector3 point2 = light.position + Vector3{
                std::cos(angle2) * light.radius,
                0.0f,
                std::sin(angle2) * light.radius
            };
            lineManager.DrawLine(point1, point2, lightColor, 0.2f);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = light.position + Vector3{
                0.0f,
                std::cos(angle1) * light.radius,
                std::sin(angle1) * light.radius
            };
            Vector3 point2 = light.position + Vector3{
                0.0f,
                std::cos(angle2) * light.radius,
                std::sin(angle2) * light.radius
            };
            lineManager.DrawLine(point1, point2, lightColor, 0.2f);
        }

        const int radialLines = 8;
        for (int j = 0; j < radialLines; ++j)
        {
            float angle = (float)j / radialLines * 2.0f * 3.14159f;

            Vector3 direction = Vector3{
                std::cos(angle),
                0.0f,
                std::sin(angle)
            };
            lineManager.DrawLine(
                light.position + direction * effectiveRadius,
                light.position + direction * light.radius,
                lightColor,
                0.3f
            );
        }
#endif
    }

    void LightDebugVisualizer::DrawSpotLightVisualization(const SpotLightData& light)
    {
        (void)light;
#ifdef _DEBUG
        if (!light.enabled) return;

        auto& lineManager = LineManager::GetInstance();
        Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

        lineManager.DrawCross(light.position, 0.8f, lightColor, 1.0f);

        Vector3 direction = MathCore::Vector::Normalize(light.direction);

        float angle = std::acos(light.cosAngle);

        float effectiveDistance = light.distance;
        if (light.decay > 0.01f)
        {
            float attenuationThreshold = 0.1f;
            float normalizedDist = std::pow(attenuationThreshold, 1.0f / light.decay);
            effectiveDistance = light.distance * (1.0f - normalizedDist);
        }

        float effectiveConeRadius = std::tan(angle) * effectiveDistance;
        float maxConeRadius = std::tan(angle) * light.distance;

        Vector3 effectiveConeEnd = light.position + direction * effectiveDistance;
        Vector3 maxConeEnd = light.position + direction * light.distance;

        Vector3 up = { 0.0f, 1.0f, 0.0f };
        if (std::abs(direction.y) > 0.99f)
        {
            up = { 1.0f, 0.0f, 0.0f };
        }
        Vector3 right = MathCore::Vector::Normalize(MathCore::Vector::Cross(up, direction));
        up = MathCore::Vector::Normalize(MathCore::Vector::Cross(direction, right));

        const int segments = 32;

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = effectiveConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * effectiveConeRadius;
            Vector3 point2 = effectiveConeEnd + (right * std::cos(angle2) + up * std::sin(angle2)) * effectiveConeRadius;

            lineManager.DrawLine(point1, point2, lightColor, 1.0f);
        }

        for (int j = 0; j < 8; ++j)
        {
            float angle1 = (float)j / 8.0f * 2.0f * 3.14159f;
            Vector3 point1 = effectiveConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * effectiveConeRadius;
            lineManager.DrawLine(light.position, point1, lightColor, 1.0f);
        }

        for (int j = 0; j < segments; ++j)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = maxConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * maxConeRadius;
            Vector3 point2 = maxConeEnd + (right * std::cos(angle2) + up * std::sin(angle2)) * maxConeRadius;

            lineManager.DrawLine(point1, point2, lightColor, 0.2f);
        }

        for (int j = 0; j < 4; ++j)
        {
            float angle1 = (float)j / 4.0f * 2.0f * 3.14159f;
            Vector3 point1 = maxConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * maxConeRadius;
            lineManager.DrawLine(light.position, point1, lightColor, 0.2f);
        }

        lineManager.DrawLine(light.position, maxConeEnd, lightColor, 1.0f);

        float falloffAngle = std::acos(light.cosFalloffStart);
        float falloffRadius = std::tan(falloffAngle) * effectiveDistance;
        Vector3 falloffEnd = light.position + direction * effectiveDistance;

        for (int j = 0; j < segments; j += 2)
        {
            float angle1 = (float)j / segments * 2.0f * 3.14159f;
            float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

            Vector3 point1 = falloffEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * falloffRadius;
            Vector3 point2 = falloffEnd + (right * std::cos(angle2) + up * std::sin(angle2)) * falloffRadius;

            lineManager.DrawLine(point1, point2, lightColor, 0.5f);
        }
#endif
    }

    void LightDebugVisualizer::DrawAreaLightVisualization(const AreaLightData& light)
    {
        (void)light;
#ifdef _DEBUG
        if (!light.enabled) return;

        auto& lineManager = LineManager::GetInstance();
        Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

        // 矩形の4つの角を計算
        Vector3 halfRight = light.right * (light.width * 0.5f);
        Vector3 halfUp = light.up * (light.height * 0.5f);

        Vector3 corner0 = light.position - halfRight - halfUp;  // 左下
        Vector3 corner1 = light.position + halfRight - halfUp;  // 右下
        Vector3 corner2 = light.position + halfRight + halfUp;  // 右上
        Vector3 corner3 = light.position - halfRight + halfUp;  // 左上

        // 矩形のエッジを描画（太線）
        lineManager.DrawLine(corner0, corner1, lightColor, 2.0f);
        lineManager.DrawLine(corner1, corner2, lightColor, 2.0f);
        lineManager.DrawLine(corner2, corner3, lightColor, 2.0f);
        lineManager.DrawLine(corner3, corner0, lightColor, 2.0f);

        // 対角線を描画（矩形の形状を明確にする）
        lineManager.DrawLine(corner0, corner2, lightColor, 0.5f);
        lineManager.DrawLine(corner1, corner3, lightColor, 0.5f);

        // 中心点を示す十字
        lineManager.DrawCross(light.position, 0.5f, lightColor, 1.5f);

        // 法線方向を示す矢印
        Vector3 normalEnd = light.position + light.normal * (light.width * 0.3f);
        lineManager.DrawLine(light.position, normalEnd, lightColor, 1.5f);

        // 法線の先端に小さな十字（方向を明確にする）
        lineManager.DrawCross(normalEnd, 0.2f, lightColor, 1.0f);

        // 範囲を示す外側の矩形（range分だけ拡大）
        float rangeScale = 1.0f + (light.range / std::max(light.width, light.height));
        Vector3 rangeHalfRight = light.right * (light.width * 0.5f * rangeScale);
        Vector3 rangeHalfUp = light.up * (light.height * 0.5f * rangeScale);

        Vector3 rangeCorner0 = light.position - rangeHalfRight - rangeHalfUp;
        Vector3 rangeCorner1 = light.position + rangeHalfRight - rangeHalfUp;
        Vector3 rangeCorner2 = light.position + rangeHalfRight + rangeHalfUp;
        Vector3 rangeCorner3 = light.position - rangeHalfRight + rangeHalfUp;

        // 範囲の外側矩形（点線風に）
        const int segments = 8;
        for (int i = 0; i < segments; ++i)
        {
            float t0 = (float)i / segments;
            float t1 = (float)(i + 1) / segments;

            // 4辺それぞれに点線を描画
            if (i % 2 == 0)
            {
                lineManager.DrawLine(
                    rangeCorner0 + (rangeCorner1 - rangeCorner0) * t0,
                    rangeCorner0 + (rangeCorner1 - rangeCorner0) * t1,
                    lightColor, 0.5f);
                lineManager.DrawLine(
                    rangeCorner1 + (rangeCorner2 - rangeCorner1) * t0,
                    rangeCorner1 + (rangeCorner2 - rangeCorner1) * t1,
                    lightColor, 0.5f);
                lineManager.DrawLine(
                    rangeCorner2 + (rangeCorner3 - rangeCorner2) * t0,
                    rangeCorner2 + (rangeCorner3 - rangeCorner2) * t1,
                    lightColor, 0.5f);
                lineManager.DrawLine(
                    rangeCorner3 + (rangeCorner0 - rangeCorner3) * t0,
                    rangeCorner3 + (rangeCorner0 - rangeCorner3) * t1,
                    lightColor, 0.5f);
            }
        }

        // 四隅から中心へのガイドライン（薄く）
        lineManager.DrawLine(corner0, light.position, lightColor, 0.3f);
        lineManager.DrawLine(corner1, light.position, lightColor, 0.3f);
        lineManager.DrawLine(corner2, light.position, lightColor, 0.3f);
        lineManager.DrawLine(corner3, light.position, lightColor, 0.3f);

        // 照射範囲を示す放射線（法線方向）
        const int rayCount = 4;
        for (int i = 0; i < rayCount; ++i)
        {
            for (int j = 0; j < rayCount; ++j)
            {
                float u = ((float)i / (rayCount - 1) - 0.5f) * light.width;
                float v = ((float)j / (rayCount - 1) - 0.5f) * light.height;

                Vector3 pointOnRect = light.position + light.right * u + light.up * v;
                Vector3 rayEnd = pointOnRect + light.normal * light.range * 0.3f;

                lineManager.DrawLine(pointOnRect, rayEnd, lightColor, 0.2f);
            }
        }
#endif
    }
}
