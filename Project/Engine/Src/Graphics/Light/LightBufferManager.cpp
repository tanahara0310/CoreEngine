#include "pch.h"
#include "LightBufferManager.h"
#include "Graphics/RootSignature/ShaderBinder.h"

#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include <cstring>

namespace CoreEngine
{
    // バッファ確保と SRV 作成をまとめて行う。最大数は起動時に決め打ちで、以後変えない
    void LightBufferManager::Initialize(
        ID3D12Device* device,
        DescriptorAllocator* descriptorAllocator,
        [[maybe_unused]] ResourceFactory* resourceFactory,
        uint32_t maxDirectionalLights,
        uint32_t maxPointLights,
        uint32_t maxSpotLights,
        uint32_t maxAreaLights
    )
    {
        CreateBufferResources(device, maxDirectionalLights, maxPointLights, maxSpotLights, maxAreaLights);

        if (descriptorAllocator)
        {
            CreateBufferSRVs(descriptorAllocator, maxDirectionalLights, maxPointLights, maxSpotLights, maxAreaLights);
        }
    }

    void LightBufferManager::UpdateBuffers(
        const std::vector<DirectionalLightData>& directionalLights,
        const std::vector<PointLightData>& pointLights,
        const std::vector<SpotLightData>& spotLights,
        const std::vector<AreaLightData>& areaLights
    )
    {
        // 種別ごとに StructuredBuffer を持つ。空の種別は Map ごと省く
        // （0 バイトの memcpy を避けるためと、未使用バッファを触らないため）
        if (!directionalLights.empty())
        {
            DirectionalLightData* mappedData = nullptr;
            directionalLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
            std::memcpy(mappedData, directionalLights.data(), sizeof(DirectionalLightData) * directionalLights.size());
            directionalLightsBuffer_->Unmap(0, nullptr);
        }

        if (!pointLights.empty())
        {
            PointLightData* mappedData = nullptr;
            pointLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
            std::memcpy(mappedData, pointLights.data(), sizeof(PointLightData) * pointLights.size());
            pointLightsBuffer_->Unmap(0, nullptr);
        }

        if (!spotLights.empty())
        {
            SpotLightData* mappedData = nullptr;
            spotLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
            std::memcpy(mappedData, spotLights.data(), sizeof(SpotLightData) * spotLights.size());
            spotLightsBuffer_->Unmap(0, nullptr);
        }

        if (!areaLights.empty())
        {
            AreaLightData* mappedData = nullptr;
            areaLightsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
            std::memcpy(mappedData, areaLights.data(), sizeof(AreaLightData) * areaLights.size());
            areaLightsBuffer_->Unmap(0, nullptr);
        }

        if (lightCountsData_)
        {
            lightCountsData_->directionalLightCount = static_cast<uint32_t>(directionalLights.size());
            lightCountsData_->pointLightCount = static_cast<uint32_t>(pointLights.size());
            lightCountsData_->spotLightCount = static_cast<uint32_t>(spotLights.size());
            lightCountsData_->areaLightCount = static_cast<uint32_t>(areaLights.size());
        }
    }

    void LightBufferManager::SetToCommandList(
        ShaderBinder& binder,
        RootSlot lightCounts,
        RootSlot directionalLights,
        RootSlot pointLights,
        RootSlot spotLights,
        RootSlot areaLights)
    {
        // 未解決スロット（そのシェーダーが参照しない種別）は ShaderBinder 側で no-op になる。
        // ハンドルが 0 のときは差せないので、ここで弾く。
        if (lightCountsBuffer_) {
            binder.Set(lightCounts, lightCountsBuffer_->GetGPUVirtualAddress());
        }
        if (directionalLightsSRVHandle_.gpuHandle.ptr != 0) {
            binder.Set(directionalLights, directionalLightsSRVHandle_.gpuHandle);
        }
        if (pointLightsSRVHandle_.gpuHandle.ptr != 0) {
            binder.Set(pointLights, pointLightsSRVHandle_.gpuHandle);
        }
        if (spotLightsSRVHandle_.gpuHandle.ptr != 0) {
            binder.Set(spotLights, spotLightsSRVHandle_.gpuHandle);
        }
        if (areaLightsSRVHandle_.gpuHandle.ptr != 0) {
            binder.Set(areaLights, areaLightsSRVHandle_.gpuHandle);
        }
    }

    void LightBufferManager::SetToCommandList(
        ID3D12GraphicsCommandList* commandList,
        int lightCountsRootParameterIndex,
        int directionalLightsRootParameterIndex,
        int pointLightsRootParameterIndex,
        int spotLightsRootParameterIndex,
        int areaLightsRootParameterIndex
    )
    {
        // ルートパラメータ番号が負なら「このシェーダーはその種別を参照しない」ので飛ばす
        if (!commandList)
        {
            return;
        }

        if (lightCountsRootParameterIndex >= 0 && lightCountsBuffer_)
        {
            commandList->SetGraphicsRootConstantBufferView(
                static_cast<UINT>(lightCountsRootParameterIndex),
                lightCountsBuffer_->GetGPUVirtualAddress()
            );
        }

        if (directionalLightsRootParameterIndex >= 0 && directionalLightsSRVHandle_.gpuHandle.ptr != 0)
        {
            commandList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(directionalLightsRootParameterIndex),
                directionalLightsSRVHandle_.gpuHandle
            );
        }

        if (pointLightsRootParameterIndex >= 0 && pointLightsSRVHandle_.gpuHandle.ptr != 0)
        {
            commandList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(pointLightsRootParameterIndex),
                pointLightsSRVHandle_.gpuHandle
            );
        }

        if (spotLightsRootParameterIndex >= 0 && spotLightsSRVHandle_.gpuHandle.ptr != 0)
        {
            commandList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(spotLightsRootParameterIndex),
                spotLightsSRVHandle_.gpuHandle
            );
        }

        if (areaLightsRootParameterIndex >= 0 && areaLightsSRVHandle_.gpuHandle.ptr != 0)
        {
            commandList->SetGraphicsRootDescriptorTable(
                static_cast<UINT>(areaLightsRootParameterIndex),
                areaLightsSRVHandle_.gpuHandle
            );
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS LightBufferManager::GetLightCountsGPUAddress() const
    {
        return lightCountsBuffer_ ? lightCountsBuffer_->GetGPUVirtualAddress() : 0;
    }

    // 種別ごとの StructuredBuffer を最大数ぶん確保する（実際の使用数は毎フレーム変わる）
    void LightBufferManager::CreateBufferResources(
        ID3D12Device* device,
        uint32_t maxDirectionalLights,
        uint32_t maxPointLights,
        uint32_t maxSpotLights,
        uint32_t maxAreaLights
    )
    {
        directionalLightsBuffer_ = ResourceFactory::CreateBufferResource(
            device,
            sizeof(DirectionalLightData) * maxDirectionalLights
        );

        pointLightsBuffer_ = ResourceFactory::CreateBufferResource(
            device,
            sizeof(PointLightData) * maxPointLights
        );

        spotLightsBuffer_ = ResourceFactory::CreateBufferResource(
            device,
            sizeof(SpotLightData) * maxSpotLights
        );

        areaLightsBuffer_ = ResourceFactory::CreateBufferResource(
            device,
            sizeof(AreaLightData) * maxAreaLights
        );

        lightCountsBuffer_ = ResourceFactory::CreateBufferResource(
            device,
            sizeof(LightCounts)
        );

        lightCountsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&lightCountsData_));
    }

    void LightBufferManager::CreateBufferSRVs(
        DescriptorAllocator* descriptorAllocator,
        uint32_t maxDirectionalLights,
        uint32_t maxPointLights,
        uint32_t maxSpotLights,
        uint32_t maxAreaLights
    )
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        srvDesc.Buffer.NumElements = maxDirectionalLights;
        srvDesc.Buffer.StructureByteStride = sizeof(DirectionalLightData);
        directionalLightsSRVHandle_ = descriptorAllocator->CreateSRV(directionalLightsBuffer_.Get(), srvDesc, "DirectionalLights");

        srvDesc.Buffer.NumElements = maxPointLights;
        srvDesc.Buffer.StructureByteStride = sizeof(PointLightData);
        pointLightsSRVHandle_ = descriptorAllocator->CreateSRV(pointLightsBuffer_.Get(), srvDesc, "PointLights");

        srvDesc.Buffer.NumElements = maxSpotLights;
        srvDesc.Buffer.StructureByteStride = sizeof(SpotLightData);
        spotLightsSRVHandle_ = descriptorAllocator->CreateSRV(spotLightsBuffer_.Get(), srvDesc, "SpotLights");

        srvDesc.Buffer.NumElements = maxAreaLights;
        srvDesc.Buffer.StructureByteStride = sizeof(AreaLightData);
        areaLightsSRVHandle_ = descriptorAllocator->CreateSRV(areaLightsBuffer_.Get(), srvDesc, "AreaLights");
    }
}
