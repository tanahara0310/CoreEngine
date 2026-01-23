#include "LightManager.h"


#include "LightData.h"
#include "MathCore.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Common/Core/DescriptorManager.h"
#include "Engine/Graphics/Line/LineManager.h"
#include <cstring>

#ifdef _DEBUG
#include <imgui.h>
#endif


namespace CoreEngine
{
	void LightManager::Initialize(ID3D12Device* device, ResourceFactory* resourceFactory, DescriptorManager* descriptorManager)
	{
		// デバイスとファクトリを保存
		device_ = device;
		resourceFactory_ = resourceFactory;

		// 新システムのStructuredBufferリソースを作成
		CreateStructuredBufferResources(device);

		// DescriptorManagerを使ってSRVを作成
		if (descriptorManager) {
			CreateStructuredBufferSRVs(descriptorManager);
		}
	}

	void LightManager::UpdateAll()
	{
		// 新システムの更新
		UpdateLightBuffers();
		// ライトのデバッグ可視化
		DrawDebugVisualization();
	}

	void LightManager::DrawAllImGui()
	{
		// ===== 新しいライトシステムのImGuiデバッグUI =====
#ifdef _DEBUG
		if (ImGui::CollapsingHeader("ライトシステム", ImGuiTreeNodeFlags_DefaultOpen)) {

			// デバッグ可視化のトグル
			ImGui::Checkbox("ライトのデバッグ可視化", &enableDebugVisualization_);
			ImGui::Separator();

			ImGui::Text("ライト統計:");
			ImGui::Text("  ディレクショナルライト: %u / %u",
				static_cast<uint32_t>(directionalLights_.size()), MAX_DIRECTIONAL_LIGHTS);
			ImGui::Text("  ポイントライト: %u / %u",
				static_cast<uint32_t>(pointLights_.size()), MAX_POINT_LIGHTS);
			ImGui::Text("  スポットライト: %u / %u",
				static_cast<uint32_t>(spotLights_.size()), MAX_SPOT_LIGHTS);
			ImGui::Text("  エリアライト: %u / %u",
				static_cast<uint32_t>(areaLights_.size()), MAX_AREA_LIGHTS);


			ImGui::Separator();

			// ディレクショナルライト
			if (ImGui::TreeNode("ディレクショナルライト")) {
				for (size_t i = 0; i < directionalLights_.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));

					if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str())) {
						auto& light = directionalLights_[i];

						ImGui::Checkbox("有効", &light.enabled);
						ImGui::ColorEdit4("色", &light.color.x);
						ImGui::DragFloat3("方向", &light.direction.x, 0.01f, -1.0f, 1.0f);
						ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);

						// 方向を正規化
						if (ImGui::Button("方向を正規化")) {
							light.direction = MathCore::Vector::Normalize(light.direction);
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				// 新しいライトを追加
				if (directionalLights_.size() < MAX_DIRECTIONAL_LIGHTS) {
					if (ImGui::Button("ディレクショナルライトを追加")) {
						AddDirectionalLight();
					}
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
				}

				ImGui::TreePop();
			}

			// ポイントライト
			if (ImGui::TreeNode("ポイントライト")) {
				for (size_t i = 0; i < pointLights_.size(); ++i) {
					ImGui::PushID(static_cast<int>(i + 1000));

					if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str())) {
						auto& light = pointLights_[i];

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

				// 新しいライトを追加
				if (pointLights_.size() < MAX_POINT_LIGHTS) {
					if (ImGui::Button("ポイントライトを追加")) {
						AddPointLight();
					}
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
				}

				ImGui::TreePop();
			}

			// スポットライト
			if (ImGui::TreeNode("スポットライト")) {
				for (size_t i = 0; i < spotLights_.size(); ++i) {
					ImGui::PushID(static_cast<int>(i + 2000));

					if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str())) {
						auto& light = spotLights_[i];

						ImGui::Checkbox("有効", &light.enabled);
						ImGui::ColorEdit4("色", &light.color.x);
						ImGui::DragFloat3("位置", &light.position.x, 0.1f, -50.0f, 50.0f);
						ImGui::DragFloat3("方向", &light.direction.x, 0.01f, -1.0f, 1.0f);
						ImGui::DragFloat("強度", &light.intensity, 0.01f, 0.0f, 10.0f);
						ImGui::DragFloat("距離", &light.distance, 0.1f, 0.1f, 100.0f);
						ImGui::DragFloat("減衰率", &light.decay, 0.01f, 0.0f, 10.0f);
						ImGui::DragFloat("角度（cos）", &light.cosAngle, 0.01f, 0.0f, 1.0f);
						ImGui::DragFloat("フォールオフ開始", &light.cosFalloffStart, 0.01f, 0.0f, 1.0f);

						// 方向を正規化
						if (ImGui::Button("方向を正規化")) {
							light.direction = MathCore::Vector::Normalize(light.direction);
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				// 新しいライトを追加
				if (spotLights_.size() < MAX_SPOT_LIGHTS) {
					if (ImGui::Button("スポットライトを追加")) {
						AddSpotLight();
					}
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
				}


				ImGui::TreePop();
			}

			// エリアライト
			if (ImGui::TreeNode("エリアライト")) {
				for (size_t i = 0; i < areaLights_.size(); ++i) {
					ImGui::PushID(static_cast<int>(i + 3000));

					if (ImGui::TreeNode(("ライト #" + std::to_string(i)).c_str())) {
						auto& light = areaLights_[i];

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

						// 方向ベクトルを正規化
						if (ImGui::Button("方向を正規化")) {
							light.direction = MathCore::Vector::Normalize(light.direction);
						}
						ImGui::SameLine();
						if (ImGui::Button("右方向を正規化")) {
							light.right = MathCore::Vector::Normalize(light.right);
						}
						ImGui::SameLine();
						if (ImGui::Button("上方向を正規化")) {
							light.up = MathCore::Vector::Normalize(light.up);
						}

						// 直交ベクトル自動生成
						if (ImGui::Button("方向から直交ベクトルを生成")) {
							// 方向ベクトルに直交する2つのベクトルを生成
							Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
							if (std::abs(light.direction.y) > 0.99f) {
								worldUp = { 1.0f, 0.0f, 0.0f };
							}
							light.right = MathCore::Vector::Normalize(MathCore::Vector::Cross(worldUp, light.direction));
							light.up = MathCore::Vector::Normalize(MathCore::Vector::Cross(light.direction, light.right));
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				// 新しいライトを追加
				if (areaLights_.size() < MAX_AREA_LIGHTS) {
					if (ImGui::Button("エリアライトを追加")) {
						AddAreaLight();
					}
				} else {
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "最大数に達しました");
				}

				ImGui::TreePop();
			}
		}
#endif // _DEBUG
	}

	// ===== 新しいStructuredBuffer方式の実装 =====

	DirectionalLightData* LightManager::AddDirectionalLight()
	{
		if (directionalLights_.size() >= MAX_DIRECTIONAL_LIGHTS) {
			return nullptr; // 最大数を超えた場合はnullptrを返す
		}

		DirectionalLightData newLight{};
		newLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		newLight.direction = { 0.0f, -1.0f, 0.0f };
		newLight.intensity = 1.0f;
		newLight.enabled = true;

		directionalLights_.push_back(newLight);
		return &directionalLights_.back();
	}

	PointLightData* LightManager::AddPointLight()
	{
		if (pointLights_.size() >= MAX_POINT_LIGHTS) {
			return nullptr;
		}

		PointLightData newLight{};
		newLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		newLight.position = { 0.0f, 0.0f, 0.0f };
		newLight.intensity = 1.0f;
		newLight.radius = 10.0f;
		newLight.decay = 1.0f;
		newLight.enabled = true;

		pointLights_.push_back(newLight);
		return &pointLights_.back();
	}

	SpotLightData* LightManager::AddSpotLight()
	{
		if (spotLights_.size() >= MAX_SPOT_LIGHTS) {
			return nullptr;
		}

		SpotLightData newLight{};
		newLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		newLight.position = { 0.0f, 0.0f, 0.0f };
		newLight.intensity = 1.0f;
		newLight.direction = { 0.0f, -1.0f, 0.0f };
		newLight.distance = 10.0f;
		newLight.decay = 1.0f;
		newLight.cosAngle = 0.5f;
		newLight.cosFalloffStart = 0.7f;
		newLight.enabled = true;

		spotLights_.push_back(newLight);
		return &spotLights_.back();
	}

	AreaLightData* LightManager::AddAreaLight()
	{
		if (areaLights_.size() >= MAX_AREA_LIGHTS) {
			return nullptr;
		}

		AreaLightData newLight{};
		newLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		newLight.position = { 0.0f, 5.0f, 0.0f };
		newLight.intensity = 1.0f;
		newLight.direction = { 0.0f, -1.0f, 0.0f };
		newLight.width = 2.0f;
		newLight.height = 2.0f;
		newLight.right = { 1.0f, 0.0f, 0.0f };
		newLight.up = { 0.0f, 0.0f, 1.0f };
		newLight.decay = 1.0f;
		newLight.enabled = true;

		areaLights_.push_back(newLight);
		return &areaLights_.back();
	}

	void LightManager::CreateStructuredBufferResources(ID3D12Device* device)
	{
		// ===== ディレクショナルライトのStructuredBuffer作成 =====
		directionalLightsBuffer_ = ResourceFactory::CreateBufferResource(
			device,
			sizeof(DirectionalLightData) * MAX_DIRECTIONAL_LIGHTS
		);

		// ===== ポイントライトのStructuredBuffer作成 =====
		pointLightsBuffer_ = ResourceFactory::CreateBufferResource(
			device,
			sizeof(PointLightData) * MAX_POINT_LIGHTS
		);

		// ===== スポットライトのStructuredBuffer作成 =====
		spotLightsBuffer_ = ResourceFactory::CreateBufferResource(
			device,
			sizeof(SpotLightData) * MAX_SPOT_LIGHTS
		);

		// ===== エリアライトのStructuredBuffer作成 =====
		areaLightsBuffer_ = ResourceFactory::CreateBufferResource(
			device,
			sizeof(AreaLightData) * MAX_AREA_LIGHTS
		);

		// ===== ライトカウント用のConstantBuffer作成 =====
		lightCountsBuffer_ = ResourceFactory::CreateBufferResource(
			device,
			sizeof(LightCounts)
		);

		// ライトカウントバッファをマップ
		lightCountsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&lightCountsData_));
	}

	void LightManager::CreateStructuredBufferSRVs(DescriptorManager* descriptorManager)
	{
		// ===== ディレクショナルライトのSRV作成 =====
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = MAX_DIRECTIONAL_LIGHTS;
		srvDesc.Buffer.StructureByteStride = sizeof(DirectionalLightData);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;

		descriptorManager->CreateSRV(directionalLightsBuffer_.Get(), srvDesc, cpuHandle, gpuHandle, "DirectionalLights");
		directionalLightsSRVHandle_ = gpuHandle;

		// ===== ポイントライトのSRV作成 =====
		srvDesc.Buffer.NumElements = MAX_POINT_LIGHTS;
		srvDesc.Buffer.StructureByteStride = sizeof(PointLightData);
		descriptorManager->CreateSRV(pointLightsBuffer_.Get(), srvDesc, cpuHandle, gpuHandle, "PointLights");
		pointLightsSRVHandle_ = gpuHandle;

		// ===== スポットライトのSRV作成 =====
		srvDesc.Buffer.NumElements = MAX_SPOT_LIGHTS;
		srvDesc.Buffer.StructureByteStride = sizeof(SpotLightData);
		descriptorManager->CreateSRV(spotLightsBuffer_.Get(), srvDesc, cpuHandle, gpuHandle, "SpotLights");
		spotLightsSRVHandle_ = gpuHandle;

		// ===== エリアライトのSRV作成 =====
		srvDesc.Buffer.NumElements = MAX_AREA_LIGHTS;
		srvDesc.Buffer.StructureByteStride = sizeof(AreaLightData);
		descriptorManager->CreateSRV(areaLightsBuffer_.Get(), srvDesc, cpuHandle, gpuHandle, "AreaLights");
		areaLightsSRVHandle_ = gpuHandle;
	}

	void LightManager::UpdateLightBuffers()
	{
		// ===== ディレクショナルライトバッファの更新 =====
		if (!directionalLights_.empty()) {
			DirectionalLightData* mappedData = nullptr;
			directionalLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
			std::memcpy(mappedData, directionalLights_.data(), sizeof(DirectionalLightData) * directionalLights_.size());
			directionalLightsBuffer_->Unmap(0, nullptr);
		}

		// ===== ポイントライトバッファの更新 =====
		if (!pointLights_.empty()) {
			PointLightData* mappedData = nullptr;
			pointLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
			std::memcpy(mappedData, pointLights_.data(), sizeof(PointLightData) * pointLights_.size());
			pointLightsBuffer_->Unmap(0, nullptr);
		}

		// ===== スポットライトバッファの更新 =====
		if (!spotLights_.empty()) {
			SpotLightData* mappedData = nullptr;
			spotLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
			std::memcpy(mappedData, spotLights_.data(), sizeof(SpotLightData) * spotLights_.size());
			spotLightsBuffer_->Unmap(0, nullptr);
		}

		// ===== エリアライトバッファの更新 =====
		if (!areaLights_.empty()) {
			AreaLightData* mappedData = nullptr;
			areaLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
			std::memcpy(mappedData, areaLights_.data(), sizeof(AreaLightData) * areaLights_.size());
			areaLightsBuffer_->Unmap(0, nullptr);
		}

		// ===== ライトカウントの更新 =====
		if (lightCountsData_) {
			lightCountsData_->directionalLightCount = static_cast<uint32_t>(directionalLights_.size());
			lightCountsData_->pointLightCount = static_cast<uint32_t>(pointLights_.size());
			lightCountsData_->spotLightCount = static_cast<uint32_t>(spotLights_.size());
			lightCountsData_->areaLightCount = static_cast<uint32_t>(areaLights_.size());
		}
	}

	void LightManager::SetLightsToCommandList(
		ID3D12GraphicsCommandList* commandList,
		UINT lightCountsRootParameterIndex,
		UINT directionalLightsRootParameterIndex,
		UINT pointLightsRootParameterIndex,
		UINT spotLightsRootParameterIndex,
		UINT areaLightsRootParameterIndex
	)
	{
		// ライトカウントをConstantBufferとしてセット
		commandList->SetGraphicsRootConstantBufferView(
			lightCountsRootParameterIndex,
			lightCountsBuffer_->GetGPUVirtualAddress()
		);

		// 各ライトのStructuredBufferをDescriptorTableとしてセット
		commandList->SetGraphicsRootDescriptorTable(
			directionalLightsRootParameterIndex,
			directionalLightsSRVHandle_
		);

		commandList->SetGraphicsRootDescriptorTable(
			pointLightsRootParameterIndex,
			pointLightsSRVHandle_
		);

		commandList->SetGraphicsRootDescriptorTable(
			spotLightsRootParameterIndex,
			spotLightsSRVHandle_
		);

		commandList->SetGraphicsRootDescriptorTable(
			areaLightsRootParameterIndex,
			areaLightsSRVHandle_
		);
	}

	D3D12_GPU_VIRTUAL_ADDRESS LightManager::GetLightCountsGPUAddress() const
	{
		return lightCountsBuffer_ ? lightCountsBuffer_->GetGPUVirtualAddress() : 0;
	}

	void LightManager::SetDirectionalLightEnabled(size_t index, bool enabled)
	{
		if (index < directionalLights_.size()) {
			directionalLights_[index].enabled = enabled;
		}
	}

	void LightManager::SetPointLightEnabled(size_t index, bool enabled)
	{
		if (index < pointLights_.size()) {
			pointLights_[index].enabled = enabled;
		}
	}

	void LightManager::SetSpotLightEnabled(size_t index, bool enabled)
	{
		if (index < spotLights_.size()) {
			spotLights_[index].enabled = enabled;
		}
	}

	void LightManager::SetAreaLightEnabled(size_t index, bool enabled)
	{
		if (index < areaLights_.size()) {
			areaLights_[index].enabled = enabled;
		}
	}

	void LightManager::ClearAllLights()
	{
		directionalLights_.clear();
		pointLights_.clear();
		spotLights_.clear();
		areaLights_.clear();
	}

	void LightManager::DrawDebugVisualization()
	{
#ifdef _DEBUG
		// デバッグ可視化が無効の場合は何もしない
		if (!enableDebugVisualization_) {
			return;
		}

		auto& lineManager = LineManager::GetInstance();

		// ===== ディレクショナルライトの可視化 =====
		for (size_t i = 0; i < directionalLights_.size(); ++i)
		{
			const auto& light = directionalLights_[i];
			if (!light.enabled) continue;

			// ライトの色を取得
			Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

			// 原点から方向を示す矢印を描画（長さ10単位）
			Vector3 origin = { 0.0f, 0.0f, 0.0f };
			Vector3 normalizedDir = MathCore::Vector::Normalize(light.direction);
			Vector3 directionEnd = origin + normalizedDir * 10.0f;

			// ===== 太い方向ライン =====
			lineManager.DrawLine(origin, directionEnd, lightColor, 1.0f);

			// ===== 矢印の先端を描画 =====
			Vector3 arrowTip = directionEnd;

			// 方向に垂直なベクトルを2つ作成
			Vector3 up = { 0.0f, 1.0f, 0.0f };
			if (std::abs(light.direction.y) > 0.99f) {
				up = { 1.0f, 0.0f, 0.0f };
			}
			Vector3 perpendicular1 = MathCore::Vector::Normalize(MathCore::Vector::Cross(light.direction, up));
			Vector3 perpendicular2 = MathCore::Vector::Normalize(MathCore::Vector::Cross(light.direction, perpendicular1));

			// 矢印の羽（8方向に増やす）
			const int arrowFeathers = 8;
			for (int j = 0; j < arrowFeathers; ++j)
			{
				float angle = (float)j / arrowFeathers * 2.0f * 3.14159f;
				Vector3 featherDir = perpendicular1 * std::cos(angle) + perpendicular2 * std::sin(angle);
				featherDir = MathCore::Vector::Normalize(featherDir) * 0.8f;

				Vector3 featherEnd = arrowTip - normalizedDir * 1.5f + featherDir;
				lineManager.DrawLine(arrowTip, featherEnd, lightColor, 1.0f);
			}

			// ===== 平行な光線を複数描画（ディレクショナルライトの特徴を表現） =====
			const int parallelRays = 5;
			float spacing = 2.0f;

			for (int x = -parallelRays / 2; x <= parallelRays / 2; ++x)
			{
				for (int z = -parallelRays / 2; z <= parallelRays / 2; ++z)
				{
					if (x == 0 && z == 0) continue; // 中心はすでに描画済み

					Vector3 offset = perpendicular1 * (float)x * spacing + perpendicular2 * (float)z * spacing;
					Vector3 rayStart = origin + offset;
					Vector3 rayEnd = rayStart + normalizedDir * 8.0f; // 少し短く

					lineManager.DrawLine(rayStart, rayEnd, lightColor, 0.3f);
				}
			}
		}

		// ===== ポイントライトの可視化 =====
		for (size_t i = 0; i < pointLights_.size(); ++i)
		{
			const auto& light = pointLights_[i];
			if (!light.enabled) continue;

			Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

			// 光源位置にクロスマーカー（大きめ）
			lineManager.DrawCross(light.position, 0.8f, lightColor, 1.0f);

			const int segments = 32;

			// ===== 減衰を考慮した実効範囲の計算 =====
			// attenuation = pow(saturate(-distance / radius + 1.0f), decay)
			// 輝度が10%になる距離を計算：0.1 = pow((radius - dist) / radius, decay)
			// 0.1^(1/decay) = (radius - dist) / radius
			// dist = radius * (1 - 0.1^(1/decay))
			float effectiveRadius = light.radius;
			if (light.decay > 0.01f) {
				float attenuationThreshold = 0.1f; // 10%の輝度
				float normalizedDist = std::pow(attenuationThreshold, 1.0f / light.decay);
				effectiveRadius = light.radius * (1.0f - normalizedDist);
			}

			// ===== 実効範囲の球（明るい範囲） =====
			// XY平面の円
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

			// XZ平面の円
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

			// YZ平面の円
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

			// ===== 最大範囲の球（薄く表示） =====
			// XY平面の円
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

			// XZ平面の円
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

			// YZ平面の円
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

			// ===== 放射状ライン（実効範囲から最大範囲へ） =====
			const int radialLines = 8;
			for (int j = 0; j < radialLines; ++j)
			{
				float angle = (float)j / radialLines * 2.0f * 3.14159f;

				// XZ平面上の放射ライン
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
		}

		// ===== スポットライトの可視化 =====
		for (size_t i = 0; i < spotLights_.size(); ++i)
		{
			const auto& light = spotLights_[i];
			if (!light.enabled) continue;

			Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

			// 光源位置にクロスマーカー（大きめ）
			lineManager.DrawCross(light.position, 0.8f, lightColor, 1.0f);

			// 方向ベクトル
			Vector3 direction = MathCore::Vector::Normalize(light.direction);

			// スポットライトの円錐を描画
			float angle = std::acos(light.cosAngle);

			// ===== 減衰を考慮した実効距離の計算 =====
			// attenuation = pow(saturate(-distance / maxDistance + 1.0f), decay)
			// 輝度が10%になる距離を計算
			float effectiveDistance = light.distance;
			if (light.decay > 0.01f) {
				float attenuationThreshold = 0.1f;
				float normalizedDist = std::pow(attenuationThreshold, 1.0f / light.decay);
				effectiveDistance = light.distance * (1.0f - normalizedDist);
			}

			float effectiveConeRadius = std::tan(angle) * effectiveDistance;
			float maxConeRadius = std::tan(angle) * light.distance;

			// 実効距離の円錐の先端位置
			Vector3 effectiveConeEnd = light.position + direction * effectiveDistance;
			Vector3 maxConeEnd = light.position + direction * light.distance;

			// 方向に垂直な2つのベクトルを作成
			Vector3 up = { 0.0f, 1.0f, 0.0f };
			if (std::abs(direction.y) > 0.99f) {
				up = { 1.0f, 0.0f, 0.0f };
			}
			Vector3 right = MathCore::Vector::Normalize(MathCore::Vector::Cross(up, direction));
			up = MathCore::Vector::Normalize(MathCore::Vector::Cross(direction, right));

			const int segments = 32;

			// ===== 実効範囲の円錐（明るい範囲・濃く） =====
			// 底面の円を描画
			for (int j = 0; j < segments; ++j)
			{
				float angle1 = (float)j / segments * 2.0f * 3.14159f;
				float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

				Vector3 point1 = effectiveConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * effectiveConeRadius;
				Vector3 point2 = effectiveConeEnd + (right * std::cos(angle2) + up * std::sin(angle2)) * effectiveConeRadius;

				lineManager.DrawLine(point1, point2, lightColor, 1.0f);
			}

			// 円錐の側面（8本）
			for (int j = 0; j < 8; ++j)
			{
				float angle1 = (float)j / 8.0f * 2.0f * 3.14159f;
				Vector3 point1 = effectiveConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * effectiveConeRadius;
				lineManager.DrawLine(light.position, point1, lightColor, 1.0f);
			}

			// ===== 最大範囲の円錐（薄く） =====
			// 底面の円を描画
			for (int j = 0; j < segments; ++j)
			{
				float angle1 = (float)j / segments * 2.0f * 3.14159f;
				float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

				Vector3 point1 = maxConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * maxConeRadius;
				Vector3 point2 = maxConeEnd + (right * std::cos(angle2) + up * std::sin(angle2)) * maxConeRadius;

				lineManager.DrawLine(point1, point2, lightColor, 0.2f);
			}

			// 円錐の側面（4本）
			for (int j = 0; j < 4; ++j)
			{
				float angle1 = (float)j / 4.0f * 2.0f * 3.14159f;
				Vector3 point1 = maxConeEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * maxConeRadius;
				lineManager.DrawLine(light.position, point1, lightColor, 0.2f);
			}

			// 中心軸を描画（太く）
			lineManager.DrawLine(light.position, maxConeEnd, lightColor, 1.0f);

			// ===== フォールオフ範囲の円錐 =====
			float falloffAngle = std::acos(light.cosFalloffStart);
			float falloffRadius = std::tan(falloffAngle) * effectiveDistance;
			Vector3 falloffEnd = light.position + direction * effectiveDistance;

			// 底面の円を描画（点線風）
			for (int j = 0; j < segments; j += 2)
			{
				float angle1 = (float)j / segments * 2.0f * 3.14159f;
				float angle2 = (float)(j + 1) / segments * 2.0f * 3.14159f;

				Vector3 point1 = falloffEnd + (right * std::cos(angle1) + up * std::sin(angle1)) * falloffRadius;
				Vector3 point2 = falloffEnd + (right * std::cos(angle2) + up * std::sin(angle2)) * falloffRadius;

				lineManager.DrawLine(point1, point2, lightColor, 0.5f);
			}
		}

		// ===== エリアライトの可視化 =====
		for (size_t i = 0; i < areaLights_.size(); ++i)
		{
			const auto& light = areaLights_[i];
			if (!light.enabled) continue;

			Vector3 lightColor = { light.color.x, light.color.y, light.color.z };

			// 中心位置にクロスマーカー（大きめ）
			lineManager.DrawCross(light.position, 0.8f, lightColor, 1.0f);

			// 矩形の4隅を計算
			Vector3 halfRight = light.right * (light.width * 0.5f);
			Vector3 halfUp = light.up * (light.height * 0.5f);

			Vector3 corner1 = light.position - halfRight - halfUp;
			Vector3 corner2 = light.position + halfRight - halfUp;
			Vector3 corner3 = light.position + halfRight + halfUp;
			Vector3 corner4 = light.position - halfRight + halfUp;

			// ===== 矩形の枠を描画（太く） =====
			lineManager.DrawLine(corner1, corner2, lightColor, 1.0f);
			lineManager.DrawLine(corner2, corner3, lightColor, 1.0f);
			lineManager.DrawLine(corner3, corner4, lightColor, 1.0f);
			lineManager.DrawLine(corner4, corner1, lightColor, 1.0f);

			// ===== 格子状のライン（矩形を分割） =====
			const int divisions = 4;

			// 横方向のライン
			for (int j = 1; j < divisions; ++j)
			{
				float t = (float)j / divisions;
				Vector3 start = corner1 + (corner4 - corner1) * t;
				Vector3 end = corner2 + (corner3 - corner2) * t;
				lineManager.DrawLine(start, end, lightColor, 0.3f);
			}

			// 縦方向のライン
			for (int j = 1; j < divisions; ++j)
			{
				float t = (float)j / divisions;
				Vector3 start = corner1 + (corner2 - corner1) * t;
				Vector3 end = corner4 + (corner3 - corner4) * t;
				lineManager.DrawLine(start, end, lightColor, 0.3f);
			}

			// 対角線を描画
			lineManager.DrawLine(corner1, corner3, lightColor, 0.2f);
			lineManager.DrawLine(corner2, corner4, lightColor, 0.2f);

			// ===== 法線方向を描画（矢印） =====
			Vector3 normalEnd = light.position + MathCore::Vector::Normalize(light.direction) * 3.0f;
			lineManager.DrawLine(light.position, normalEnd, lightColor, 1.0f);

			// 法線の矢印（4方向の羽）
			Vector3 arrowBase = normalEnd - MathCore::Vector::Normalize(light.direction) * 0.8f;
			lineManager.DrawLine(normalEnd, arrowBase + light.right * 0.3f, lightColor, 1.0f);
			lineManager.DrawLine(normalEnd, arrowBase - light.right * 0.3f, lightColor, 1.0f);
			lineManager.DrawLine(normalEnd, arrowBase + light.up * 0.3f, lightColor, 1.0f);
			lineManager.DrawLine(normalEnd, arrowBase - light.up * 0.3f, lightColor, 1.0f);

			// ===== シェーダーと同じ計算で影響範囲を可視化 =====
			// maxDistance = max(width, height) * 1.5f（シェーダーと同じ）
			float maxDistance = (light.width > light.height ? light.width : light.height) * 1.5f;

			// 法線方向を正規化
			Vector3 normalizedDir = MathCore::Vector::Normalize(light.direction);

			// ===== グリッド状にサンプル点を配置して照明範囲を可視化 =====
			const int gridSize = 20; // グリッドの解像度
			const float gridRange = maxDistance * 1.2f; // グリッドの範囲を少し広めに
			const float gridSpacing = gridRange * 2.0f / gridSize;

			for (int gx = -gridSize / 2; gx <= gridSize / 2; ++gx)
			{
				for (int gy = -gridSize / 2; gy <= gridSize / 2; ++gy)
				{
					for (int gz = 1; gz <= gridSize / 2; ++gz) // 法線方向の前方のみ
					{
						// グリッド上のサンプル点を計算
						Vector3 samplePoint = light.position +
							light.right * (gx * gridSpacing) +
							light.up * (gy * gridSpacing) +
							normalizedDir * (gz * gridSpacing);

						// ===== シェーダーと完全に同じ計算 =====
						// ライト中心から表面への方向ベクトル
						Vector3 toSurface = samplePoint - light.position;

						// ライト平面との距離（法線方向の成分）
						float distAlongNormal = MathCore::Vector::Dot(toSurface, normalizedDir);

						// ライトの裏側にある場合はスキップ
						if (distAlongNormal <= 0.0f)
						{
							continue;
						}

						// 表面の点をライト平面に投影
						Vector3 projectedPoint = samplePoint - normalizedDir * distAlongNormal;

						// 投影された点の矩形ローカル座標系での位置
						Vector3 localPos = projectedPoint - light.position;
						float u = MathCore::Vector::Dot(localPos, light.right);
						float v = MathCore::Vector::Dot(localPos, light.up);

						// 矩形の範囲内にクランプ
						float clampedU = (std::max)(-light.width * 0.5f, (std::min)(light.width * 0.5f, u));
						float clampedV = (std::max)(-light.height * 0.5f, (std::min)(light.height * 0.5f, v));

						// 矩形上の最近接点
						Vector3 closestPoint = light.position + light.right * clampedU + light.up * clampedV;

						// 最近接点への方向と距離（シェーダーと同じ）
						Vector3 lightDir = closestPoint - samplePoint;
						float distance = MathCore::Vector::Length(lightDir);
						lightDir = MathCore::Vector::Normalize(lightDir);

						// 距離による減衰（シェーダーと同じ計算）
						float distanceAttenuation = std::pow((std::max)(0.0f, (std::min)(1.0f, 1.0f - distance / maxDistance)), light.decay);

						// ライト平面の法線との角度による減衰（ライトが表面を向いているか）
						float normalDot = (std::max)(0.0f, (std::min)(1.0f, MathCore::Vector::Dot(-lightDir, normalizedDir)));

						// 最終的な減衰
						float attenuation = distanceAttenuation * normalDot;

						// 強度も考慮した最終的な明るさ
						float finalBrightness = attenuation * light.intensity;

						// 閾値以上なら描画（強度を考慮）
						if (finalBrightness >= 0.05f)
						{
							// サンプル点を間引いて描画（パフォーマンス対策）
							if ((gx + gy + gz) % 2 == 0)
							{
								// クロスサイズは明るさに応じて変化
								float crossSize = 0.05f + finalBrightness * 0.15f;
								// 透明度も明るさに応じて変化
								float alpha = (std::min)(1.0f, finalBrightness);
								lineManager.DrawCross(samplePoint, crossSize, lightColor, alpha);
							}
						}
					}
				}
			}

			// ===== 影響範囲の境界を描画 =====
			// 複数の同心矩形を描画して範囲を表現
			const int boundaryLevels = 4;
			for (int level = 1; level <= boundaryLevels; ++level)
			{
				float dist = maxDistance * level / boundaryLevels;
				Vector3 offset = normalizedDir * dist;

				// このレベルでの減衰を計算（シェーダーと同じ）
				float levelAttenuation = std::pow((std::max)(0.0f, 1.0f - dist / maxDistance), light.decay);
				float levelBrightness = levelAttenuation * light.intensity;

				// 十分な明るさがある場合のみ描画
				if (levelBrightness >= 0.05f)
				{
					float alpha = (std::min)(1.0f, levelBrightness);

					// 矩形を平行移動
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

			// ===== 元の矩形から最大範囲への接続線（4隅のみ） =====
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
		}
#endif
	}
}


