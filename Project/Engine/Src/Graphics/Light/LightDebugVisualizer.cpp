#include "LightDebugVisualizer.h"

#include "Math/MathCore.h"
#include "Graphics/Line/LineManager.h"
#include <cmath>

#ifdef _DEBUG
#include <imgui.h>
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
        if (ImGui::CollapsingHeader("ライトシステム", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("ライトのデバッグ可視化", &enableVisualization_);
            ImGui::Separator();

            ImGui::Text("ライト統計:");
            ImGui::Text("  ディレクショナルライト: %u / %u",
                static_cast<uint32_t>(directionalLights.size()), maxDirectionalLights);
            ImGui::Text("  ポイントライト: %u / %u",
                static_cast<uint32_t>(pointLights.size()), maxPointLights);
            ImGui::Text("  スポットライト: %u / %u",
                static_cast<uint32_t>(spotLights.size()), maxSpotLights);
            ImGui::Text("  エリアライト: %u / %u",
                static_cast<uint32_t>(areaLights.size()), maxAreaLights);

            ImGui::Separator();

            if (ImGui::TreeNode("ディレクショナルライト"))
            {
                for (size_t i = 0; i < directionalLights.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));

                    if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str()))
                    {
                        auto& light = directionalLights[i];

                        ImGui::Checkbox("有効", &light.enabled);
                        ImGui::ColorEdit4("色", &light.color.x);
                        ImGui::DragFloat3("方向", &light.direction.x, 0.01f, -1.0f, 1.0f);
                        ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);

                        if (ImGui::Button("方向を正規化"))
                        {
                            light.direction = MathCore::Vector::Normalize(light.direction);
                        }

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                if (directionalLights.size() < maxDirectionalLights)
                {
                    if (ImGui::Button("ディレクショナルライトを追加"))
                    {
                        if (onAddDirectionalLight) onAddDirectionalLight();
                    }
                } else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("ポイントライト"))
            {
                for (size_t i = 0; i < pointLights.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i + 1000));

                    if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str()))
                    {
                        auto& light = pointLights[i];

                        ImGui::Checkbox("有効", &light.enabled);
                        ImGui::ColorEdit4("色", &light.color.x);
                        ImGui::DragFloat3("位置", &light.position.x, 0.1f, -50.0f, 50.0f);
                        ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("半径", &light.radius, 0.1f, 0.1f, 100.0f);
                        ImGui::DragFloat("減衰率", &light.decay, 0.01f, 0.0f, 10.0f);

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                if (pointLights.size() < maxPointLights)
                {
                    if (ImGui::Button("ポイントライトを追加"))
                    {
                        if (onAddPointLight) onAddPointLight();
                    }
                } else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("スポットライト"))
            {
                for (size_t i = 0; i < spotLights.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i + 2000));

                    if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str()))
                    {
                        auto& light = spotLights[i];

                        ImGui::Checkbox("有効", &light.enabled);
                        ImGui::ColorEdit4("色", &light.color.x);
                        ImGui::DragFloat3("位置", &light.position.x, 0.1f, -50.0f, 50.0f);
                        ImGui::DragFloat3("方向", &light.direction.x, 0.01f, -1.0f, 1.0f);
                        ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("距離", &light.distance, 0.1f, 0.1f, 100.0f);
                        ImGui::DragFloat("減衰率", &light.decay, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("角度（cos）", &light.cosAngle, 0.01f, 0.0f, 1.0f);
                        ImGui::DragFloat("フォールオフ開始", &light.cosFalloffStart, 0.01f, 0.0f, 1.0f);

                        if (ImGui::Button("方向を正規化"))
                        {
                            light.direction = MathCore::Vector::Normalize(light.direction);
                        }

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                if (spotLights.size() < maxSpotLights)
                {
                    if (ImGui::Button("スポットライトを追加"))
                    {
                        if (onAddSpotLight) onAddSpotLight();
                    }
                } else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("エリアライト"))
            {
                for (size_t i = 0; i < areaLights.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i + 3000));

                    if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str()))
                    {
                        auto& light = areaLights[i];

                        ImGui::Checkbox("有効", &light.enabled);
                        ImGui::ColorEdit4("色", &light.color.x);
                        ImGui::DragFloat3("位置", &light.position.x, 0.1f, -50.0f, 50.0f);
                        ImGui::DragFloat3("法線", &light.normal.x, 0.01f, -1.0f, 1.0f);
                        ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);
                        ImGui::DragFloat("幅", &light.width, 0.1f, 0.1f, 20.0f);
                        ImGui::DragFloat("高さ", &light.height, 0.1f, 0.1f, 20.0f);
                        ImGui::DragFloat("範囲", &light.range, 0.1f, 0.1f, 100.0f);
                        ImGui::DragFloat3("右ベクトル", &light.right.x, 0.01f, -1.0f, 1.0f);
                        ImGui::DragFloat3("上ベクトル", &light.up.x, 0.01f, -1.0f, 1.0f);

                        if (ImGui::Button("法線を正規化"))
                        {
                            light.normal = MathCore::Vector::Normalize(light.normal);
                        }

                        ImGui::TreePop();
                    }

                    ImGui::PopID();
                }

                if (areaLights.size() < maxAreaLights)
                {
                    if (ImGui::Button("エリアライトを追加"))
                    {
                        if (onAddAreaLight) onAddAreaLight();
                    }
                } else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
                }

                ImGui::TreePop();
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
