#include "LightManager.h"

#include "LightData.h"
#include "MathCore.h"

namespace CoreEngine
{
	void LightManager::Initialize(ID3D12Device* device, ResourceFactory* resourceFactory, DescriptorManager* descriptorManager)
	{
		bufferManager_.Initialize(
			device,
			descriptorManager,
			resourceFactory,
			MAX_DIRECTIONAL_LIGHTS,
			MAX_POINT_LIGHTS,
			MAX_SPOT_LIGHTS,
			MAX_AREA_LIGHTS
		);
	}

	void LightManager::UpdateAll()
	{
		bufferManager_.UpdateBuffers(directionalLights_, pointLights_, spotLights_, areaLights_);
		debugVisualizer_.DrawVisualization(directionalLights_, pointLights_, spotLights_, areaLights_);
	}

	void LightManager::DrawAllImGui()
	{
		auto onAddDirectionalLight = [this]() { AddDirectionalLight(); };
		auto onAddPointLight = [this]() { AddPointLight(); };
		auto onAddSpotLight = [this]() { AddSpotLight(); };
		auto onAddAreaLight = [this]() { AddAreaLight(); };

		debugVisualizer_.DrawImGui(
			directionalLights_,
			pointLights_,
			spotLights_,
			areaLights_,
			MAX_DIRECTIONAL_LIGHTS,
			MAX_POINT_LIGHTS,
			MAX_SPOT_LIGHTS,
			MAX_AREA_LIGHTS,
			onAddDirectionalLight,
			onAddPointLight,
			onAddSpotLight,
			onAddAreaLight
		);
	}

	DirectionalLightData* LightManager::AddDirectionalLight()
	{
		if (directionalLights_.size() >= MAX_DIRECTIONAL_LIGHTS)
		{
			return nullptr;
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
		if (pointLights_.size() >= MAX_POINT_LIGHTS)
		{
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
		if (spotLights_.size() >= MAX_SPOT_LIGHTS)
		{
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
		if (areaLights_.size() >= MAX_AREA_LIGHTS)
		{
			return nullptr;
		}

		AreaLightData newLight{};
		newLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
		newLight.position = { 0.0f, 0.0f, 0.0f };
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

	void LightManager::SetLightsToCommandList(
		ID3D12GraphicsCommandList* commandList,
		UINT lightCountsRootParameterIndex,
		UINT directionalLightsRootParameterIndex,
		UINT pointLightsRootParameterIndex,
		UINT spotLightsRootParameterIndex,
		UINT areaLightsRootParameterIndex
	)
	{
		bufferManager_.SetToCommandList(
			commandList,
			lightCountsRootParameterIndex,
			directionalLightsRootParameterIndex,
			pointLightsRootParameterIndex,
			spotLightsRootParameterIndex,
			areaLightsRootParameterIndex
		);
	}

	D3D12_GPU_VIRTUAL_ADDRESS LightManager::GetLightCountsGPUAddress() const
	{
		return bufferManager_.GetLightCountsGPUAddress();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetDirectionalLightsSRVHandle() const
	{
		return bufferManager_.GetDirectionalLightsSRVHandle();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetPointLightsSRVHandle() const
	{
		return bufferManager_.GetPointLightsSRVHandle();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetSpotLightsSRVHandle() const
	{
		return bufferManager_.GetSpotLightsSRVHandle();
	}

	D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetAreaLightsSRVHandle() const
	{
		return bufferManager_.GetAreaLightsSRVHandle();
	}

	void LightManager::SetDirectionalLightEnabled(size_t index, bool enabled)
	{
		if (index < directionalLights_.size())
		{
			directionalLights_[index].enabled = enabled;
		}
	}

	void LightManager::SetPointLightEnabled(size_t index, bool enabled)
	{
		if (index < pointLights_.size())
		{
			pointLights_[index].enabled = enabled;
		}
	}

	void LightManager::SetSpotLightEnabled(size_t index, bool enabled)
	{
		if (index < spotLights_.size())
		{
			spotLights_[index].enabled = enabled;
		}
	}

	void LightManager::SetAreaLightEnabled(size_t index, bool enabled)
	{
		if (index < areaLights_.size())
		{
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
}
