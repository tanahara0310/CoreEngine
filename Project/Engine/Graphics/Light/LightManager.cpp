#include "LightManager.h"

#include "LightData.h"
#include "MathCore.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/Common/Core/DescriptorManager.h"
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
}

void LightManager::DrawAllImGui()
{
    // ===== 新しいライトシステムのImGuiデバッグUI =====
#ifdef _DEBUG
    if (ImGui::CollapsingHeader("ライトシステム", ImGuiTreeNodeFlags_DefaultOpen)) {
        
        ImGui::Text("ライト統計:");
        ImGui::Text("  ディレクショナルライト: %u / %u", 
            static_cast<uint32_t>(directionalLights_.size()), MAX_DIRECTIONAL_LIGHTS);
        ImGui::Text("  ポイントライト: %u / %u", 
            static_cast<uint32_t>(pointLights_.size()), MAX_POINT_LIGHTS);
        ImGui::Text("  スポットライト: %u / %u", 
            static_cast<uint32_t>(spotLights_.size()), MAX_SPOT_LIGHTS);
        
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

    // ===== ライトカウントの更新 =====
    if (lightCountsData_) {
        lightCountsData_->directionalLightCount = static_cast<uint32_t>(directionalLights_.size());
        lightCountsData_->pointLightCount = static_cast<uint32_t>(pointLights_.size());
        lightCountsData_->spotLightCount = static_cast<uint32_t>(spotLights_.size());
    }
}

void LightManager::SetLightsToCommandList(
    ID3D12GraphicsCommandList* commandList,
    UINT lightCountsRootParameterIndex,
    UINT directionalLightsRootParameterIndex,
    UINT pointLightsRootParameterIndex,
    UINT spotLightsRootParameterIndex
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

void LightManager::ClearAllLights()
{
    directionalLights_.clear();
    pointLights_.clear();
    spotLights_.clear();
}
}
