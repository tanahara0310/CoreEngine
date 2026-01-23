#include "LightDebugVisualizer.h"

#include "MathCore.h"
#include "Engine/Graphics/Line/LineManager.h"
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
				}
				else
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
				}
				else
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
				}
				else
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
						ImGui::DragFloat3("方向（法線）", &light.direction.x, 0.01f, -1.0f, 1.0f);
						ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);
						ImGui::DragFloat("幅", &light.width, 0.1f, 0.1f, 20.0f);
						ImGui::DragFloat("高さ", &light.height, 0.1f, 0.1f, 20.0f);
						ImGui::DragFloat3("右方向", &light.right.x, 0.01f, -1.0f, 1.0f);
						ImGui::DragFloat3("上方向", &light.up.x, 0.01f, -1.0f, 1.0f);
						ImGui::DragFloat("減衰率", &light.decay, 0.01f, 0.0f, 10.0f);

						if (ImGui::Button("方向を正規化"))
						{
							light.direction = MathCore::Vector::Normalize(light.direction);
						}
						ImGui::SameLine();
						if (ImGui::Button("右方向を正規化"))
						{
							light.right = MathCore::Vector::Normalize(light.right);
						}
						ImGui::SameLine();
						if (ImGui::Button("上方向を正規化"))
						{
							light.up = MathCore::Vector::Normalize(light.up);
						}

						if (ImGui::Button("方向から直交ベクトルを生成"))
						{
							Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
							if (std::abs(light.direction.y) > 0.99f)
							{
								worldUp = { 1.0f, 0.0f, 0.0f };
							}
							light.right = MathCore::Vector::Normalize(MathCore::Vector::Cross(worldUp, light.direction));
							light.up = MathCore::Vector::Normalize(MathCore::Vector::Cross(light.direction, light.right));
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
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
				}

				ImGui::TreePop();
			}
		}
#endif
	}

	void LightDebugVisualizer::DrawVisualization(
		const std::vector<DirectionalLightData>& directionalLights,
		const std::vector<PointLightData>& pointLights,
		const std::vector<SpotLightData>& spotLights,
		const std::vector<AreaLightData>& areaLights
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

		for (const auto& light : areaLights)
		{
			DrawAreaLightVisualization(light);
		}
#endif
	}

	void LightDebugVisualizer::DrawDirectionalLightVisualization(const DirectionalLightData& light)
	{
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
#ifdef _DEBUG
		if (!light.enabled) return;

		auto& lineManager = LineManager::GetInstance();
		Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

		lineManager.DrawCross(light.position, 0.8f, lightColor, 1.0f);

		Vector3 halfRight = light.right * (light.width * 0.5f);
		Vector3 halfUp = light.up * (light.height * 0.5f);

		Vector3 corner1 = light.position - halfRight - halfUp;
		Vector3 corner2 = light.position + halfRight - halfUp;
		Vector3 corner3 = light.position + halfRight + halfUp;
		Vector3 corner4 = light.position - halfRight + halfUp;

		lineManager.DrawLine(corner1, corner2, lightColor, 1.0f);
		lineManager.DrawLine(corner2, corner3, lightColor, 1.0f);
		lineManager.DrawLine(corner3, corner4, lightColor, 1.0f);
		lineManager.DrawLine(corner4, corner1, lightColor, 1.0f);

		const int divisions = 4;

		for (int j = 1; j < divisions; ++j)
		{
			float t = (float)j / divisions;
			Vector3 start = corner1 + (corner4 - corner1) * t;
			Vector3 end = corner2 + (corner3 - corner2) * t;
			lineManager.DrawLine(start, end, lightColor, 0.3f);
		}

		for (int j = 1; j < divisions; ++j)
		{
			float t = (float)j / divisions;
			Vector3 start = corner1 + (corner2 - corner1) * t;
			Vector3 end = corner4 + (corner3 - corner4) * t;
			lineManager.DrawLine(start, end, lightColor, 0.3f);
		}

		lineManager.DrawLine(corner1, corner3, lightColor, 0.2f);
		lineManager.DrawLine(corner2, corner4, lightColor, 0.2f);

		Vector3 normalEnd = light.position + MathCore::Vector::Normalize(light.direction) * 3.0f;
		lineManager.DrawLine(light.position, normalEnd, lightColor, 1.0f);

		Vector3 arrowBase = normalEnd - MathCore::Vector::Normalize(light.direction) * 0.8f;
		lineManager.DrawLine(normalEnd, arrowBase + light.right * 0.3f, lightColor, 1.0f);
		lineManager.DrawLine(normalEnd, arrowBase - light.right * 0.3f, lightColor, 1.0f);
		lineManager.DrawLine(normalEnd, arrowBase + light.up * 0.3f, lightColor, 1.0f);
		lineManager.DrawLine(normalEnd, arrowBase - light.up * 0.3f, lightColor, 1.0f);

		float maxDistance = (light.width > light.height ? light.width : light.height) * 1.5f;

		Vector3 normalizedDir = MathCore::Vector::Normalize(light.direction);

		const int gridSize = 20;
		const float gridRange = maxDistance * 1.2f;
		const float gridSpacing = gridRange * 2.0f / gridSize;

		for (int gx = -gridSize / 2; gx <= gridSize / 2; ++gx)
		{
			for (int gy = -gridSize / 2; gy <= gridSize / 2; ++gy)
			{
				for (int gz = 1; gz <= gridSize / 2; ++gz)
				{
					Vector3 samplePoint = light.position +
						light.right * (gx * gridSpacing) +
						light.up * (gy * gridSpacing) +
						normalizedDir * (gz * gridSpacing);

					Vector3 toSurface = samplePoint - light.position;

					float distAlongNormal = MathCore::Vector::Dot(toSurface, normalizedDir);

					if (distAlongNormal <= 0.0f)
					{
						continue;
					}

					Vector3 projectedPoint = samplePoint - normalizedDir * distAlongNormal;

					Vector3 localPos = projectedPoint - light.position;
					float u = MathCore::Vector::Dot(localPos, light.right);
					float v = MathCore::Vector::Dot(localPos, light.up);

					float clampedU = (std::max)(-light.width * 0.5f, (std::min)(light.width * 0.5f, u));
					float clampedV = (std::max)(-light.height * 0.5f, (std::min)(light.height * 0.5f, v));

					Vector3 closestPoint = light.position + light.right * clampedU + light.up * clampedV;

					Vector3 lightDir = closestPoint - samplePoint;
					float distance = MathCore::Vector::Length(lightDir);
					lightDir = MathCore::Vector::Normalize(lightDir);

					float distanceAttenuation = std::pow((std::max)(0.0f, (std::min)(1.0f, 1.0f - distance / maxDistance)), light.decay);

					float normalDot = (std::max)(0.0f, (std::min)(1.0f, MathCore::Vector::Dot(-lightDir, normalizedDir)));

					float attenuation = distanceAttenuation * normalDot;

					float finalBrightness = attenuation * light.intensity;

					if (finalBrightness >= 0.05f)
					{
						if ((gx + gy + gz) % 2 == 0)
						{
							float crossSize = 0.05f + finalBrightness * 0.15f;
							float alpha = (std::min)(1.0f, finalBrightness);
							lineManager.DrawCross(samplePoint, crossSize, lightColor, alpha);
						}
					}
				}
			}
		}

		const int boundaryLevels = 4;
		for (int level = 1; level <= boundaryLevels; ++level)
		{
			float dist = maxDistance * level / boundaryLevels;
			Vector3 offset = normalizedDir * dist;

			float levelAttenuation = std::pow((std::max)(0.0f, 1.0f - dist / maxDistance), light.decay);
			float levelBrightness = levelAttenuation * light.intensity;

			if (levelBrightness >= 0.05f)
			{
				float alpha = (std::min)(1.0f, levelBrightness);

				Vector3 c1 = corner1 + offset;
				Vector3 c2 = corner2 + offset;
				Vector3 c3 = corner3 + offset;
				Vector3 c4 = corner4 + offset;

				lineManager.DrawLine(c1, c2, lightColor, alpha * 0.6f);
				lineManager.DrawLine(c2, c3, lightColor, alpha * 0.6f);
				lineManager.DrawLine(c3, c4, lightColor, alpha * 0.6f);
				lineManager.DrawLine(c4, c1, lightColor, alpha * 0.6f);
			}
		}

		{
			Vector3 offset = normalizedDir * maxDistance;

			Vector3 c1 = corner1 + offset;
			Vector3 c2 = corner2 + offset;
			Vector3 c3 = corner3 + offset;
			Vector3 c4 = corner4 + offset;

			lineManager.DrawLine(corner1, c1, lightColor, 0.2f);
			lineManager.DrawLine(corner2, c2, lightColor, 0.2f);
			lineManager.DrawLine(corner3, c3, lightColor, 0.2f);
			lineManager.DrawLine(corner4, c4, lightColor, 0.2f);
		}
#endif
	}
}
