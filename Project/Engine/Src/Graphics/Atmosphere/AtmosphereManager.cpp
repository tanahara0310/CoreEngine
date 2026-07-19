#include "pch.h"
#include "AtmosphereManager.h"

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace {
        constexpr float kMetersToKm = 1.0f / 1000.0f;
        constexpr float kPerMeterToPerKm = 1000.0f;

        /// @brief HLSL の smoothstep と同じ（エルミート補間）
        float SmoothStep(float edge0, float edge1, float x)
        {
            const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        D3D12_RESOURCE_DESC MakeLUTTexture2DDesc(uint32_t width, uint32_t height, DXGI_FORMAT format)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }
    }

    void AtmosphereManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager)
    {
        device_ = device;
        descriptorManager_ = descriptorManager;

        // 大気散乱定数バッファ（永続マップ）
        constantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(AtmosphereShaderConstants));
        constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantData_));
        UploadConstants();

        // LUT リソースとコンピュートパイプライン
        const bool lutResourcesReady = CreateLUTResources(device, descriptorManager);
        pipelinesReady_ = lutResourcesReady && CreateLUTPipelines(device);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "AtmosphereManager: 初期化完了 (惑星半径={:.0f}m, 大気圏上端={:.0f}m, LUTパイプライン={})",
            parameters_.planetRadius, parameters_.atmosphereTopRadius,
            pipelinesReady_ ? "OK" : "無効");
    }

    bool AtmosphereManager::CreateLUTResources(ID3D12Device* device, DescriptorManager* descriptorManager)
    {
        if (!device || !descriptorManager) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        const D3D12_RESOURCE_DESC transmittanceDesc = MakeLUTTexture2DDesc(
            kTransmittanceLUTWidth, kTransmittanceLUTHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);

        try {
            transmittanceLUT_ = ResourceFactory::CreateTextureResource(
                deviceRef, transmittanceDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Transmittance LUT テクスチャの生成に失敗");
            return false;
        }
        transmittanceState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = transmittanceDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = transmittanceDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        descriptorManager->CreateSRV(transmittanceLUT_.Get(), srvDesc, cpuHandle, transmittanceSrvHandle_, "AtmosphereTransmittanceSRV");
        descriptorManager->CreateUAV(transmittanceLUT_.Get(), uavDesc, cpuHandle, transmittanceUavHandle_, "AtmosphereTransmittanceUAV");

        // ===== Multi-Scattering LUT =====
        const D3D12_RESOURCE_DESC multiScatteringDesc = MakeLUTTexture2DDesc(
            kMultiScatteringLUTSize, kMultiScatteringLUTSize, DXGI_FORMAT_R16G16B16A16_FLOAT);

        try {
            multiScatteringLUT_ = ResourceFactory::CreateTextureResource(
                deviceRef, multiScatteringDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Multi-Scattering LUT テクスチャの生成に失敗");
            return false;
        }
        multiScatteringState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        srvDesc.Format = multiScatteringDesc.Format;
        uavDesc.Format = multiScatteringDesc.Format;
        descriptorManager->CreateSRV(multiScatteringLUT_.Get(), srvDesc, cpuHandle, multiScatteringSrvHandle_, "AtmosphereMultiScatteringSRV");
        descriptorManager->CreateUAV(multiScatteringLUT_.Get(), uavDesc, cpuHandle, multiScatteringUavHandle_, "AtmosphereMultiScatteringUAV");

        // ===== Sky-View LUT =====
        const D3D12_RESOURCE_DESC skyViewDesc = MakeLUTTexture2DDesc(
            kSkyViewLUTWidth, kSkyViewLUTHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);

        try {
            skyViewLUT_ = ResourceFactory::CreateTextureResource(
                deviceRef, skyViewDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Sky-View LUT テクスチャの生成に失敗");
            return false;
        }
        skyViewState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        srvDesc.Format = skyViewDesc.Format;
        uavDesc.Format = skyViewDesc.Format;
        descriptorManager->CreateSRV(skyViewLUT_.Get(), srvDesc, cpuHandle, skyViewSrvHandle_, "AtmosphereSkyViewSRV");
        descriptorManager->CreateUAV(skyViewLUT_.Get(), uavDesc, cpuHandle, skyViewUavHandle_, "AtmosphereSkyViewUAV");

        // ===== Camera Volume LUT（froxel 3D テクスチャ） =====
        D3D12_RESOURCE_DESC volumeDesc{};
        volumeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        volumeDesc.Width = kCameraVolumeSize;
        volumeDesc.Height = kCameraVolumeSize;
        volumeDesc.DepthOrArraySize = kCameraVolumeSize;
        volumeDesc.MipLevels = 1;
        volumeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        volumeDesc.SampleDesc.Count = 1;
        volumeDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        volumeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            cameraVolumeLUT_ = ResourceFactory::CreateTextureResource(
                deviceRef, volumeDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Camera Volume LUT テクスチャの生成に失敗");
            return false;
        }
        cameraVolumeState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_SHADER_RESOURCE_VIEW_DESC volumeSrvDesc{};
        volumeSrvDesc.Format = volumeDesc.Format;
        volumeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        volumeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        volumeSrvDesc.Texture3D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC volumeUavDesc{};
        volumeUavDesc.Format = volumeDesc.Format;
        volumeUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        volumeUavDesc.Texture3D.WSize = kCameraVolumeSize;

        descriptorManager->CreateSRV(cameraVolumeLUT_.Get(), volumeSrvDesc, cpuHandle, cameraVolumeSrvHandle_, "AtmosphereCameraVolumeSRV");
        descriptorManager->CreateUAV(cameraVolumeLUT_.Get(), volumeUavDesc, cpuHandle, cameraVolumeUavHandle_, "AtmosphereCameraVolumeUAV");

        // ===== 空アンビエント SH9 係数バッファ（StructuredBuffer<float4> × 9） =====
        {
            constexpr uint32_t kCoeffStride = sizeof(float) * 4;

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC bufferDesc{};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = kCoeffStride * kSkyIrradianceSHCoeffCount;
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            if (FAILED(device->CreateCommittedResource(
                    &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                    IID_PPV_ARGS(&skyIrradianceSHBuffer_)))) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "AtmosphereManager: 空アンビエント SH バッファの生成に失敗");
                return false;
            }
            skyIrradianceState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            D3D12_SHADER_RESOURCE_VIEW_DESC shSrvDesc{};
            shSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
            shSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            shSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            shSrvDesc.Buffer.FirstElement = 0;
            shSrvDesc.Buffer.NumElements = kSkyIrradianceSHCoeffCount;
            shSrvDesc.Buffer.StructureByteStride = kCoeffStride;

            D3D12_UNORDERED_ACCESS_VIEW_DESC shUavDesc{};
            shUavDesc.Format = DXGI_FORMAT_UNKNOWN;
            shUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            shUavDesc.Buffer.FirstElement = 0;
            shUavDesc.Buffer.NumElements = kSkyIrradianceSHCoeffCount;
            shUavDesc.Buffer.StructureByteStride = kCoeffStride;

            descriptorManager->CreateSRV(skyIrradianceSHBuffer_.Get(), shSrvDesc, cpuHandle, skyIrradianceSrvHandle_, "AtmosphereSkyIrradianceSHSRV");
            descriptorManager->CreateUAV(skyIrradianceSHBuffer_.Get(), shUavDesc, cpuHandle, skyIrradianceUavHandle_, "AtmosphereSkyIrradianceSHUAV");
        }

        // ===== 空キューブマップ（空＋雲の作業面。Phase 3b スペキュラIBL） =====
        {
            D3D12_RESOURCE_DESC cubeDesc{};
            cubeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            cubeDesc.Width = kSkyCubemapSize;
            cubeDesc.Height = kSkyCubemapSize;
            cubeDesc.DepthOrArraySize = 6;
            cubeDesc.MipLevels = 1;
            cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            cubeDesc.SampleDesc.Count = 1;
            cubeDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            cubeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            try {
                skyCubemap_ = ResourceFactory::CreateTextureResource(
                    deviceRef, cubeDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "AtmosphereManager: 空キューブマップの生成に失敗");
                return false;
            }
            skyCubemapState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            D3D12_SHADER_RESOURCE_VIEW_DESC cubeSrvDesc{};
            cubeSrvDesc.Format = cubeDesc.Format;
            cubeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            cubeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            cubeSrvDesc.TextureCube.MipLevels = 1;

            D3D12_UNORDERED_ACCESS_VIEW_DESC cubeUavDesc{};
            cubeUavDesc.Format = cubeDesc.Format;
            cubeUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            cubeUavDesc.Texture2DArray.MipSlice = 0;
            cubeUavDesc.Texture2DArray.FirstArraySlice = 0;
            cubeUavDesc.Texture2DArray.ArraySize = 6;

            descriptorManager->CreateSRV(skyCubemap_.Get(), cubeSrvDesc, cpuHandle, skyCubemapSrvHandle_, "AtmosphereSkyCubemapSRV");
            descriptorManager->CreateUAV(skyCubemap_.Get(), cubeUavDesc, cpuHandle, skyCubemapUavHandle_, "AtmosphereSkyCubemapUAV");
        }

        // ===== プリフィルタ済み空スペキュラキューブマップ =====
        {
            D3D12_RESOURCE_DESC specDesc{};
            specDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            specDesc.Width = kSkyCubemapSize;
            specDesc.Height = kSkyCubemapSize;
            specDesc.DepthOrArraySize = 6;
            specDesc.MipLevels = static_cast<UINT16>(kSkySpecularMipCount);
            specDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            specDesc.SampleDesc.Count = 1;
            specDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            specDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            try {
                skySpecularMap_ = ResourceFactory::CreateTextureResource(
                    deviceRef, specDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "AtmosphereManager: 空スペキュラキューブマップの生成に失敗");
                return false;
            }
            skySpecularState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            D3D12_SHADER_RESOURCE_VIEW_DESC specSrvDesc{};
            specSrvDesc.Format = specDesc.Format;
            specSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            specSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            specSrvDesc.TextureCube.MipLevels = kSkySpecularMipCount;

            descriptorManager->CreateSRV(skySpecularMap_.Get(), specSrvDesc, cpuHandle, skySpecularSrvHandle_, "AtmosphereSkySpecularSRV");

            for (uint32_t mip = 0; mip < kSkySpecularMipCount; ++mip) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC specUavDesc{};
                specUavDesc.Format = specDesc.Format;
                specUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                specUavDesc.Texture2DArray.MipSlice = mip;
                specUavDesc.Texture2DArray.FirstArraySlice = 0;
                specUavDesc.Texture2DArray.ArraySize = 6;
                descriptorManager->CreateUAV(skySpecularMap_.Get(), specUavDesc, cpuHandle,
                    skySpecularUavHandles_[mip], "AtmosphereSkySpecularUAV");
            }
        }

        // ===== プリフィルタのミップ別パラメータ CB（256B アライメントスロット × ミップ数） =====
        {
            struct SkyPrefilterParams {
                float roughness;
                float pad0;
                uint32_t outputWidth;
                uint32_t outputHeight;
            };
            constexpr uint32_t kSlotSize = 256; // CBV アライメント
            skyPrefilterParamsCB_ = ResourceFactory::CreateBufferResource(
                device, kSlotSize * kSkySpecularMipCount);

            uint8_t* mapped = nullptr;
            skyPrefilterParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
            for (uint32_t mip = 0; mip < kSkySpecularMipCount; ++mip) {
                auto* slot = reinterpret_cast<SkyPrefilterParams*>(mapped + kSlotSize * mip);
                slot->roughness = static_cast<float>(mip) / static_cast<float>(kSkySpecularMipCount - 1);
                slot->pad0 = 0.0f;
                slot->outputWidth = kSkyCubemapSize >> mip;
                slot->outputHeight = kSkyCubemapSize >> mip;
            }
            skyPrefilterParamsCB_->Unmap(0, nullptr);
        }

        return true;
    }

    bool AtmosphereManager::CreateLUTPipelines(ID3D12Device* device)
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool transmittanceBuilt = transmittancePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, transmittanceShaderProvider_);

        if (!transmittanceBuilt || !transmittancePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Transmittance LUT コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool multiScatteringBuilt = multiScatteringPipeline_.Build(
            device, shaderCompiler, reflectionBuilder, multiScatteringShaderProvider_);

        if (!multiScatteringBuilt || !multiScatteringPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Multi-Scattering LUT コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool skyViewBuilt = skyViewPipeline_.Build(
            device, shaderCompiler, reflectionBuilder, skyViewShaderProvider_);

        if (!skyViewBuilt || !skyViewPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Sky-View LUT コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool cameraVolumeBuilt = cameraVolumePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, cameraVolumeShaderProvider_);

        if (!cameraVolumeBuilt || !cameraVolumePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Camera Volume LUT コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool aerialPerspectiveBuilt = aerialPerspectivePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, aerialPerspectiveShaderProvider_);

        if (!aerialPerspectiveBuilt || !aerialPerspectivePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Aerial Perspective コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool skyIrradianceBuilt = skyIrradiancePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, skyIrradianceShaderProvider_);

        if (!skyIrradianceBuilt || !skyIrradiancePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: 空アンビエント SH コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool skyEnvCaptureBuilt = skyEnvironmentCapturePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, skyEnvironmentCaptureShaderProvider_);

        if (!skyEnvCaptureBuilt || !skyEnvironmentCapturePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: 空キューブマップ焼き込みコンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool skyEnvPrefilterBuilt = skyEnvironmentPrefilterPipeline_.Build(
            device, shaderCompiler, reflectionBuilder, skyEnvironmentPrefilterShaderProvider_);

        if (!skyEnvPrefilterBuilt || !skyEnvironmentPrefilterPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: 空キューブマッププリフィルタコンピュートパイプラインの構築に失敗");
            return false;
        }

        return true;
    }

    void AtmosphereManager::GenerateLUTsIfNeeded(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !pipelinesReady_) {
            return;
        }
        if (!paramsDirty_ && !skyViewDirty_ && !cameraVolumeDirty_) {
            return;
        }

        // Sky-View / Camera Volume は Transmittance / Multi-Scattering を参照するため、
        // パラメータ変更時は先に2つを再生成する
        if (paramsDirty_) {
            GenerateBaseLUTs(cmdList);
        }
        if (paramsDirty_ || skyViewDirty_) {
            GenerateSkyViewLUT(cmdList);
            // 空アンビエント SH は Sky-View LUT の内容から射影するため、直後に再生成する
            GenerateSkyIrradianceSH(cmdList);
            // 空キューブマップ（スペキュラIBL）も Sky-View LUT から焼くため再生成を要求する
            skyEnvironmentDirty_ = true;
        }
        // Camera Volume は太陽・パラメータ・カメラ姿勢のいずれの変化でも再生成が必要
        GenerateCameraVolumeLUT(cmdList);

        paramsDirty_ = false;
        skyViewDirty_ = false;
        cameraVolumeDirty_ = false;
        lutsGenerated_ = true;
    }

    void AtmosphereManager::GenerateBaseLUTs(ID3D12GraphicsCommandList* cmdList)
    {
        // ===== Transmittance LUT =====
        ResourceBarrierHelper::Transition(cmdList, transmittanceLUT_.Get(),
            transmittanceState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(transmittancePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(transmittancePipeline_.GetComputeRootSignature());

        const int cbSlot = transmittancePipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int uavSlot = transmittancePipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
        if (uavSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(uavSlot), transmittanceUavHandle_);
        }

        cmdList->Dispatch(
            (kTransmittanceLUTWidth + 7) / 8,
            (kTransmittanceLUTHeight + 7) / 8,
            1);

        // Multi-Scattering パスが SRV として読めるよう遷移
        ResourceBarrierHelper::UAV(cmdList, transmittanceLUT_.Get());
        ResourceBarrierHelper::Transition(cmdList, transmittanceLUT_.Get(),
            transmittanceState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== Multi-Scattering LUT（Transmittance LUT を参照） =====
        ResourceBarrierHelper::Transition(cmdList, multiScatteringLUT_.Get(),
            multiScatteringState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(multiScatteringPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(multiScatteringPipeline_.GetComputeRootSignature());

        const int msCbSlot = multiScatteringPipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (msCbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(msCbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int msSrvSlot = multiScatteringPipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
        if (msSrvSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(msSrvSlot), transmittanceSrvHandle_);
        }
        const int msUavSlot = multiScatteringPipeline_.GetComputeRootParamIndex("gMultiScatteringLUT");
        if (msUavSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(msUavSlot), multiScatteringUavHandle_);
        }

        cmdList->Dispatch(
            (kMultiScatteringLUTSize + 7) / 8,
            (kMultiScatteringLUTSize + 7) / 8,
            1);

        // 描画シェーダーから参照できるよう SRV 状態へ遷移
        ResourceBarrierHelper::UAV(cmdList, multiScatteringLUT_.Get());
        ResourceBarrierHelper::Transition(cmdList, multiScatteringLUT_.Get(),
            multiScatteringState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void AtmosphereManager::GenerateSkyViewLUT(ID3D12GraphicsCommandList* cmdList)
    {
        ResourceBarrierHelper::Transition(cmdList, skyViewLUT_.Get(),
            skyViewState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(skyViewPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(skyViewPipeline_.GetComputeRootSignature());

        const int cbSlot = skyViewPipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int transmittanceSlot = skyViewPipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
        if (transmittanceSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(transmittanceSlot), transmittanceSrvHandle_);
        }
        const int multiScatteringSlot = skyViewPipeline_.GetComputeRootParamIndex("gMultiScatteringLUT");
        if (multiScatteringSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(multiScatteringSlot), multiScatteringSrvHandle_);
        }
        const int uavSlot = skyViewPipeline_.GetComputeRootParamIndex("gSkyViewLUT");
        if (uavSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(uavSlot), skyViewUavHandle_);
        }

        cmdList->Dispatch(
            (kSkyViewLUTWidth + 7) / 8,
            (kSkyViewLUTHeight + 7) / 8,
            1);

        ResourceBarrierHelper::UAV(cmdList, skyViewLUT_.Get());
        ResourceBarrierHelper::Transition(cmdList, skyViewLUT_.Get(),
            skyViewState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void AtmosphereManager::GenerateSkyIrradianceSH(ID3D12GraphicsCommandList* cmdList)
    {
        if (!skyIrradianceSHBuffer_) {
            return;
        }

        // GenerateSkyViewLUT 直後に呼ばれる前提（Sky-View LUT は SRV 状態）
        ResourceBarrierHelper::Transition(cmdList, skyIrradianceSHBuffer_.Get(),
            skyIrradianceState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(skyIrradiancePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(skyIrradiancePipeline_.GetComputeRootSignature());

        const int cbSlot = skyIrradiancePipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int skyViewSlot = skyIrradiancePipeline_.GetComputeRootParamIndex("gSkyViewLUT");
        if (skyViewSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(skyViewSlot), skyViewSrvHandle_);
        }
        const int uavSlot = skyIrradiancePipeline_.GetComputeRootParamIndex("gSkyIrradianceSH");
        if (uavSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(uavSlot), skyIrradianceUavHandle_);
        }

        // シェーダー側が 1 グループで全方向を分担する
        cmdList->Dispatch(1, 1, 1);

        // DeferredLighting（ピクセルシェーダー）から読めるよう SRV 状態へ遷移
        ResourceBarrierHelper::UAV(cmdList, skyIrradianceSHBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, skyIrradianceSHBuffer_.Get(),
            skyIrradianceState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        skyIrradianceGenerated_ = true;
    }

    bool AtmosphereManager::ConsumeSkyEnvironmentDirty(bool cloudsActive, uint64_t frameNumber)
    {
        if (!pipelinesReady_ || !lutsGenerated_ || !skySpecularEnabled_ || !skyCubemap_) {
            return false;
        }
        // 同一フレームの補助 View（反射など）での二重実行を防ぐ
        if (frameNumber == lastSkyEnvironmentFrame_) {
            return false;
        }
        // 雲は風で毎フレーム動くため、雲が有効な間は常時再生成する
        if (!skyEnvironmentDirty_ && !cloudsActive) {
            return false;
        }
        lastSkyEnvironmentFrame_ = frameNumber;
        skyEnvironmentDirty_ = false;
        return true;
    }

    void AtmosphereManager::CaptureSkyEnvironment(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !skyCubemap_) {
            return;
        }

        ResourceBarrierHelper::Transition(cmdList, skyCubemap_.Get(),
            skyCubemapState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(skyEnvironmentCapturePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(skyEnvironmentCapturePipeline_.GetComputeRootSignature());

        const int cbSlot = skyEnvironmentCapturePipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int skyViewSlot = skyEnvironmentCapturePipeline_.GetComputeRootParamIndex("gSkyViewLUT");
        if (skyViewSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(skyViewSlot), skyViewSrvHandle_);
        }
        const int uavSlot = skyEnvironmentCapturePipeline_.GetComputeRootParamIndex("gSkyCubemap");
        if (uavSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(uavSlot), skyCubemapUavHandle_);
        }

        cmdList->Dispatch(
            (kSkyCubemapSize + 7) / 8,
            (kSkyCubemapSize + 7) / 8,
            6);

        // 直後に雲の前乗算合成（同じ UAV への読み書き）が入るため UAV バリアで区切る
        ResourceBarrierHelper::UAV(cmdList, skyCubemap_.Get());
    }

    void AtmosphereManager::PrefilterSkyEnvironment(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !skyCubemap_ || !skySpecularMap_ || !skyPrefilterParamsCB_) {
            return;
        }

        // 入力（空＋雲キューブマップ）を SRV、出力ミップ群を UAV へ
        ResourceBarrierHelper::Transition(cmdList, skyCubemap_.Get(),
            skyCubemapState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, skySpecularMap_.Get(),
            skySpecularState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(skyEnvironmentPrefilterPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(skyEnvironmentPrefilterPipeline_.GetComputeRootSignature());

        const int cbSlot = skyEnvironmentPrefilterPipeline_.GetComputeRootParamIndex("SkyPrefilterParams");
        const int srvSlot = skyEnvironmentPrefilterPipeline_.GetComputeRootParamIndex("gSkyCubemap");
        const int uavSlot = skyEnvironmentPrefilterPipeline_.GetComputeRootParamIndex("gSkySpecularMap");

        if (srvSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(srvSlot), skyCubemapSrvHandle_);
        }

        constexpr uint32_t kSlotSize = 256;
        const D3D12_GPU_VIRTUAL_ADDRESS paramsBase = skyPrefilterParamsCB_->GetGPUVirtualAddress();
        for (uint32_t mip = 0; mip < kSkySpecularMipCount; ++mip) {
            if (cbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(cbSlot), paramsBase + kSlotSize * mip);
            }
            if (uavSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(
                    static_cast<UINT>(uavSlot), skySpecularUavHandles_[mip]);
            }
            const uint32_t mipSize = kSkyCubemapSize >> mip;
            cmdList->Dispatch((mipSize + 7) / 8, (mipSize + 7) / 8, 6);
        }

        // ピクセルシェーダー（DeferredLighting / Water.PS）から読める状態へ
        ResourceBarrierHelper::UAV(cmdList, skySpecularMap_.Get());
        ResourceBarrierHelper::Transition(cmdList, skySpecularMap_.Get(),
            skySpecularState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        skyEnvironmentGenerated_ = true;
    }

    void AtmosphereManager::GenerateCameraVolumeLUT(ID3D12GraphicsCommandList* cmdList)
    {
        ResourceBarrierHelper::Transition(cmdList, cameraVolumeLUT_.Get(),
            cameraVolumeState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(cameraVolumePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(cameraVolumePipeline_.GetComputeRootSignature());

        const int cbSlot = cameraVolumePipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int transmittanceSlot = cameraVolumePipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
        if (transmittanceSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(transmittanceSlot), transmittanceSrvHandle_);
        }
        const int multiScatteringSlot = cameraVolumePipeline_.GetComputeRootParamIndex("gMultiScatteringLUT");
        if (multiScatteringSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(multiScatteringSlot), multiScatteringSrvHandle_);
        }
        const int uavSlot = cameraVolumePipeline_.GetComputeRootParamIndex("gCameraVolumeLUT");
        if (uavSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(uavSlot), cameraVolumeUavHandle_);
        }

        cmdList->Dispatch(
            (kCameraVolumeSize + 3) / 4,
            (kCameraVolumeSize + 3) / 4,
            (kCameraVolumeSize + 3) / 4);

        ResourceBarrierHelper::UAV(cmdList, cameraVolumeLUT_.Get());
        ResourceBarrierHelper::Transition(cmdList, cameraVolumeLUT_.Get(),
            cameraVolumeState_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    bool AtmosphereManager::EnsureAerialPerspectiveTarget(ID3D12Resource* sceneColor)
    {
        if (!sceneColor || !device_ || !descriptorManager_) {
            return false;
        }

        const D3D12_RESOURCE_DESC sceneDesc = sceneColor->GetDesc();
        if (apResult_ && apResultWidth_ == sceneDesc.Width && apResultHeight_ == sceneDesc.Height) {
            return true;
        }

        // SceneColor と同サイズ・同フォーマットの UAV 対応中間テクスチャを確保する
        D3D12_RESOURCE_DESC desc = sceneDesc;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device_;
        try {
            apResult_ = ResourceFactory::CreateTextureResource(
                deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "AtmosphereManager: Aerial Perspective 中間テクスチャの生成に失敗");
            return false;
        }
        apResultState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        apResultWidth_ = sceneDesc.Width;
        apResultHeight_ = sceneDesc.Height;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        // リサイズによる再生成時は既存スロットへ書き直す（毎回確保するとスロットリーク）
        descriptorManager_->CreateOrUpdateUAV(apResult_.Get(), uavDesc, apResultUavCpuHandle_, apResultUavHandle_, "AtmosphereAerialPerspectiveUAV");

        return true;
    }

    void AtmosphereManager::ApplyAerialPerspective(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* sceneColor,
        D3D12_RESOURCE_STATES& sceneColorState,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle)
    {
        if (!cmdList || !pipelinesReady_ || !lutsGenerated_) {
            return;
        }
        if (!EnsureAerialPerspectiveTarget(sceneColor)) {
            return;
        }

        // ===== 合成 CS: SceneColor + SceneDepth + CameraVolume → 中間テクスチャ =====
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, apResult_.Get(),
            apResultState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(aerialPerspectivePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(aerialPerspectivePipeline_.GetComputeRootSignature());

        const int cbSlot = aerialPerspectivePipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int sceneColorSlot = aerialPerspectivePipeline_.GetComputeRootParamIndex("gSceneColor");
        if (sceneColorSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(sceneColorSlot), sceneColorSrvHandle);
        }
        const int depthSlot = aerialPerspectivePipeline_.GetComputeRootParamIndex("gSceneDepth");
        if (depthSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(depthSlot), depthSrvHandle);
        }
        const int volumeSlot = aerialPerspectivePipeline_.GetComputeRootParamIndex("gCameraVolumeLUT");
        if (volumeSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(volumeSlot), cameraVolumeSrvHandle_);
        }
        const int outputSlot = aerialPerspectivePipeline_.GetComputeRootParamIndex("gOutput");
        if (outputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(outputSlot), apResultUavHandle_);
        }

        cmdList->Dispatch(
            (static_cast<UINT>(apResultWidth_) + 7) / 8,
            (apResultHeight_ + 7) / 8,
            1);

        // ===== 結果を SceneColor へコピーバック =====
        ResourceBarrierHelper::UAV(cmdList, apResult_.Get());
        ResourceBarrierHelper::Transition(cmdList, apResult_.Get(),
            apResultState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor, apResult_.Get());

        // 後続パス（GeometryPass の RT 描画）に備えて元の想定状態へ戻す
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, apResult_.Get(),
            apResultState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    void AtmosphereManager::Update(const Vector3& cameraWorldPosition,
                                   const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
                                   LightManager* lightManager)
    {
        // Update() を呼ぶのは大気を使うシーンのみ。このフレームは大気合成を有効にする。
        atmosphereActive_ = true;

        // ===== Aerial Perspective 用カメラ情報 =====
        cameraWorldPos_ = cameraWorldPosition;
        invViewProj_ = MathCore::Matrix::Inverse(
            MathCore::Matrix::Multiply(viewMatrix, projMatrix));

        // カメラ姿勢が変わった場合のみ Camera Volume を再生成する
        if (std::memcmp(&invViewProj_, &lastInvViewProj_, sizeof(Matrix4x4)) != 0) {
            cameraVolumeDirty_ = true;
            lastInvViewProj_ = invViewProj_;
        }

        // ===== 太陽情報の取得 =====
        hasSunLight_ = false;
        if (lightManager) {
            if (Light* sun = lightManager->GetAtmosphereSunLight()) {
                sunDirection_ = MathCore::Vector::Normalize(sun->direction);
                sunColor_ = { sun->color.x, sun->color.y, sun->color.z, 1.0f };
                // 空の輝度スケールは atmosphereIntensity（サーフェス直接光の照度 [lx] とは単位系が別）。
                // 0（未設定）の場合は照度をシェーダー単位へ換算した値にフォールバックする
                // （旧仕様「intensity へフォールバック」の物理単位版。旧 intensity=1.0 ≒ 57,143 lx）。
                sunIntensity_ = (sun->atmosphereIntensity > 0.0f)
                    ? sun->atmosphereIntensity : LightUnits::LuxToShader(sun->intensity);
                hasSunLight_ = sun->enabled;
            }
        }

        // ===== 月情報の取得（第2大気ライト。オプトイン） =====
        hasMoonLight_ = false;
        if (lightManager) {
            if (Light* moon = lightManager->GetAtmosphereMoonLight()) {
                moonDirection_ = MathCore::Vector::Normalize(moon->direction);
                moonColor_ = { moon->color.x, moon->color.y, moon->color.z, 1.0f };
                moonIntensity_ = (moon->atmosphereIntensity > 0.0f)
                    ? moon->atmosphereIntensity : LightUnits::LuxToShader(moon->intensity);
                hasMoonLight_ = moon->enabled && moonIntensity_ > 0.0f;
            }
        }

        // ===== カメラ高度 → 惑星中心距離の変換 =====
        // ワールド全体を惑星スケールへは変換しない。Y座標のみを高度として扱う。
        cameraHeightAboveGround_ = cameraWorldPosition.y - parameters_.groundLevelY;

        // 惑星中心距離は地表以上・大気圏上端以下へクランプする
        // （地表下へ潜った場合や大気圏を突き抜けた場合に積分が破綻しないようにする）
        const float minRadius = parameters_.planetRadius + 1.0f; // 地表すれすれの特異点回避に +1m
        const float maxRadius = parameters_.atmosphereTopRadius - 1.0f;
        distanceFromPlanetCenter_ = std::clamp(
            parameters_.planetRadius + cameraHeightAboveGround_,
            minRadius, maxRadius);

        // ===== Sky-View LUT の変化検知 =====
        // 太陽方向・色・強度・カメラ高度が変わった場合のみ Sky-View を再生成する
        // （Transmittance / Multi-Scattering は太陽に依存しないため再生成しない）。
        // 色・強度が対象なのは、Sky-View / CameraVolume LUT にライト色を前乗算して
        // 格納しているため（サンプル時乗算だった旧方式では方向のみで足りた）
        constexpr float kDirEpsilon = 1e-5f;
        constexpr float kRadiusEpsilonMeters = 0.5f;
        constexpr float kColorEpsilon = 1e-5f;
        const bool sunChanged =
            std::abs(sunDirection_.x - lastSunDirection_.x) > kDirEpsilon ||
            std::abs(sunDirection_.y - lastSunDirection_.y) > kDirEpsilon ||
            std::abs(sunDirection_.z - lastSunDirection_.z) > kDirEpsilon ||
            std::abs(sunColor_.x - lastSunColor_.x) > kColorEpsilon ||
            std::abs(sunColor_.y - lastSunColor_.y) > kColorEpsilon ||
            std::abs(sunColor_.z - lastSunColor_.z) > kColorEpsilon ||
            std::abs(sunIntensity_ - lastSunIntensity_) > kColorEpsilon;
        // 月も同様（月の有無自体の切替も再生成対象）
        const bool moonChanged =
            hasMoonLight_ != lastHasMoon_ ||
            (hasMoonLight_ && (
                std::abs(moonDirection_.x - lastMoonDirection_.x) > kDirEpsilon ||
                std::abs(moonDirection_.y - lastMoonDirection_.y) > kDirEpsilon ||
                std::abs(moonDirection_.z - lastMoonDirection_.z) > kDirEpsilon ||
                std::abs(moonColor_.x - lastMoonColor_.x) > kColorEpsilon ||
                std::abs(moonColor_.y - lastMoonColor_.y) > kColorEpsilon ||
                std::abs(moonColor_.z - lastMoonColor_.z) > kColorEpsilon ||
                std::abs(moonIntensity_ - lastMoonIntensity_) > kColorEpsilon));
        const bool cameraChanged =
            std::abs(distanceFromPlanetCenter_ - lastCameraRadius_) > kRadiusEpsilonMeters;
        if (sunChanged || moonChanged || cameraChanged) {
            skyViewDirty_ = true;
            lastSunDirection_ = sunDirection_;
            lastCameraRadius_ = distanceFromPlanetCenter_;
            lastSunColor_ = sunColor_;
            lastSunIntensity_ = sunIntensity_;
            lastMoonDirection_ = moonDirection_;
            lastMoonColor_ = moonColor_;
            lastMoonIntensity_ = moonIntensity_;
            lastHasMoon_ = hasMoonLight_;
        }

        // ===== 直接光の大気透過率（Transmittance on Light。太陽・月共通） =====
        // 地表→光源の透過率でライトの実効色を変調する（UE の Atmosphere Sun Light 相当）。
        // authored なライトデータは書き換えず、LightManager が GPU 転送時のコピーへ適用する
        // （直接書き換えると翌フレームに減衰済みの色を再度読み、フィードバックで光が消えていく）。
        // ライトの GPU 転送（FrameStart）は本 Update（PostLogic）より先に走るため反映は
        // 1 フレーム遅延だが、光源方向は連続的にしか変化しないため知覚できない。
        sunTransmittance_ = hasSunLight_
            ? ComputeLightTransmittanceCPU(sunDirection_) : Vector3{ 1.0f, 1.0f, 1.0f };
        moonTransmittance_ = hasMoonLight_
            ? ComputeLightTransmittanceCPU(moonDirection_) : Vector3{ 1.0f, 1.0f, 1.0f };
        if (lightManager) {
            lightManager->SetAtmosphereSunTransmittance(
                transmittanceOnLight_ ? sunTransmittance_ : Vector3{ 1.0f, 1.0f, 1.0f });
            lightManager->SetAtmosphereMoonTransmittance(
                transmittanceOnLight_ ? moonTransmittance_ : Vector3{ 1.0f, 1.0f, 1.0f });
        }

        // ===== 照明駆動露出用のシーン代表輝度 =====
        sceneIlluminationLuminance_ = ComputeSceneIlluminationLuminance();

        // ===== 定数バッファ更新 =====
        UploadConstants();
    }

    float AtmosphereManager::ComputeSceneIlluminationLuminance() const
    {
        // 光源1灯（intensity=1）あたりの相対地表照度。
        //  - 高度 >= 0: 地平線で 0.1（直達が水平でも空の散乱光がある）→ 天頂で 1.0 の線形。
        //  - 高度 < 0（薄明）: 実測の薄明照度カーブに合わせた指数減衰
        //    （太陽高度1°につき約10^0.36倍。市民薄明-6°で日没時の約1/150、
        //     天文薄明-18°で実質ゼロ。文献値の log-linear 近似）。
        auto relativeIlluminance = [](float sinElevation) {
            if (sinElevation >= 0.0f) {
                return 0.1f + 0.9f * sinElevation;
            }
            const float elevationDeg =
                std::asin(std::clamp(sinElevation, -1.0f, 1.0f)) * (180.0f / 3.14159265358979323846f);
            return 0.1f * std::pow(10.0f, 0.36f * elevationDeg); // elevationDeg < 0
        };
        auto lumaOf = [](const Vector4& c) {
            return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
        };

        float illuminance = 0.0f;
        if (hasSunLight_) {
            // toSun.y = -direction.y = sin(高度)
            illuminance += sunIntensity_ * lumaOf(sunColor_) * relativeIlluminance(-sunDirection_.y);
        }
        if (hasMoonLight_) {
            illuminance += moonIntensity_ * lumaOf(moonColor_) * relativeIlluminance(-moonDirection_.y);
        }

        // 屋外の代表的な画面平均輝度への変換係数。
        // 昼（太陽高度30°・intensity=20）で従来の画面平均測光の実測順応輝度（≈1.4）と
        // 一致するよう校正した値（照明駆動へ切り替えても昼の見た目が変わらない）。
        constexpr float kSceneReflectanceScale = 0.127f;

        // 星明かり・大気光相当の下限（月なしの夜の代表輝度。0 だと EV がクランプに
        // 張り付くだけなので、Krawczyk キーが意味を持つ微小な床を与える）
        constexpr float kStarlightFloorLuminance = 2e-4f;

        return std::max(illuminance * kSceneReflectanceScale, kStarlightFloorLuminance);
    }

    Vector3 AtmosphereManager::ComputeLightTransmittanceCPU(const Vector3& lightDirection) const
    {
        // 単位系は HLSL 側（AtmosphereCommon.hlsli）と同じ km / 1_km
        const float planetRadiusKm = parameters_.planetRadius * kMetersToKm;
        const float topRadiusKm = parameters_.atmosphereTopRadius * kMetersToKm;
        const float radiusKm = planetRadiusKm + 0.002f; // 地表 +2m（地表すれすれの特異点回避）

        // 評価点は (0, radiusKm, 0)。光源天頂角余弦は toSun.y に一致する（太陽・月共用）
        const Vector3 toSun = { -lightDirection.x, -lightDirection.y, -lightDirection.z };
        const float mu = toSun.y;

        // 惑星本体による遮蔽（HLSL PlanetSunVisibility と同一式）。
        // 光源は角半径を持つため、地平線を跨ぐ間は smoothstep で滑らかに減衰する
        // （太陽 0.265°・月 0.259°でソフトネス差は知覚できないため太陽の視半径で共用する）
        const float cosHorizon = -std::sqrt(std::max(0.0f,
            1.0f - (planetRadiusKm * planetRadiusKm) / (radiusKm * radiusKm)));
        const float softness = std::max(
            parameters_.sunDiskAngularRadiusDeg * 3.14159265358979323846f / 180.0f, 1e-4f);
        const float visibility = SmoothStep(cosHorizon - softness, cosHorizon + softness, mu);
        if (visibility <= 0.0f) {
            return { 0.0f, 0.0f, 0.0f };
        }

        // 大気圏上端までの距離（評価点は大気圏内なので正の根が必ず存在する）
        const float b = radiusKm * mu;
        const float c = radiusKm * radiusKm - topRadiusKm * topRadiusKm;
        const float tTop = -b + std::sqrt(std::max(0.0f, b * b - c));
        if (tTop <= 0.0f) {
            return { visibility, visibility, visibility };
        }

        // 係数を 1/km へ変換（UploadConstants と同じ変換）
        const Vector3 rayleigh = {
            parameters_.rayleighScattering.x * kPerMeterToPerKm,
            parameters_.rayleighScattering.y * kPerMeterToPerKm,
            parameters_.rayleighScattering.z * kPerMeterToPerKm };
        const float mieExtinction =
            (parameters_.mieScattering + parameters_.mieAbsorption) * kPerMeterToPerKm;
        const Vector3 ozone = {
            parameters_.ozoneAbsorption.x * kPerMeterToPerKm,
            parameters_.ozoneAbsorption.y * kPerMeterToPerKm,
            parameters_.ozoneAbsorption.z * kPerMeterToPerKm };
        const float rayleighScaleHeightKm = std::max(parameters_.rayleighScaleHeight * kMetersToKm, 1e-3f);
        const float mieScaleHeightKm = std::max(parameters_.mieScaleHeight * kMetersToKm, 1e-3f);
        const float ozoneCenterKm = parameters_.ozoneLayerCenter * kMetersToKm;
        const float ozoneHalfWidthKm = std::max(parameters_.ozoneLayerHalfWidth * kMetersToKm, 1e-3f);

        // 光学的深度の数値積分（HLSL ComputeTransmittanceToSunRayMarch と同じ中点則 40 ステップ）
        constexpr int kStepCount = 40;
        const float dt = tTop / kStepCount;
        Vector3 opticalDepth = { 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < kStepCount; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) * dt;
            const float px = toSun.x * t;
            const float py = radiusKm + toSun.y * t;
            const float pz = toSun.z * t;
            const float heightKm = std::max(
                std::sqrt(px * px + py * py + pz * pz) - planetRadiusKm, 0.0f);

            const float densityRayleigh = std::exp(-heightKm / rayleighScaleHeightKm);
            const float densityMie = std::exp(-heightKm / mieScaleHeightKm);
            const float densityOzone = std::clamp(
                1.0f - std::abs(heightKm - ozoneCenterKm) / ozoneHalfWidthKm, 0.0f, 1.0f);

            opticalDepth.x += (rayleigh.x * densityRayleigh + mieExtinction * densityMie + ozone.x * densityOzone) * dt;
            opticalDepth.y += (rayleigh.y * densityRayleigh + mieExtinction * densityMie + ozone.y * densityOzone) * dt;
            opticalDepth.z += (rayleigh.z * densityRayleigh + mieExtinction * densityMie + ozone.z * densityOzone) * dt;
        }

        return {
            std::exp(-opticalDepth.x) * visibility,
            std::exp(-opticalDepth.y) * visibility,
            std::exp(-opticalDepth.z) * visibility };
    }

    void AtmosphereManager::UploadConstants()
    {
        if (!constantData_) {
            return;
        }

        AtmosphereShaderConstants constants{};
        constants.sunDirection = sunDirection_;
        constants.sunIntensity = sunIntensity_;
        constants.sunColor = { sunColor_.x, sunColor_.y, sunColor_.z };
        constants.planetRadiusKm = parameters_.planetRadius * kMetersToKm;
        constants.rayleighScattering = parameters_.rayleighScattering * kPerMeterToPerKm;
        constants.rayleighScaleHeightKm = parameters_.rayleighScaleHeight * kMetersToKm;
        constants.ozoneAbsorption = parameters_.ozoneAbsorption * kPerMeterToPerKm;
        constants.atmosphereTopRadiusKm = parameters_.atmosphereTopRadius * kMetersToKm;
        constants.mieScattering = parameters_.mieScattering * kPerMeterToPerKm;
        constants.mieAbsorption = parameters_.mieAbsorption * kPerMeterToPerKm;
        constants.mieScaleHeightKm = parameters_.mieScaleHeight * kMetersToKm;
        constants.miePhaseG = parameters_.miePhaseG;
        constants.ozoneLayerCenterKm = parameters_.ozoneLayerCenter * kMetersToKm;
        constants.ozoneLayerHalfWidthKm = parameters_.ozoneLayerHalfWidth * kMetersToKm;
        constants.cameraRadiusKm = distanceFromPlanetCenter_ * kMetersToKm;
        constants.sunDiskHalfAngleRad = parameters_.sunDiskAngularRadiusDeg * 3.14159265358979323846f / 180.0f;
        constants.groundAlbedo = parameters_.groundAlbedo;
        constants.sunDiskLuminanceScale = parameters_.sunDiskLuminanceScale;
        constants.invViewProj = invViewProj_;
        constants.cameraWorldPos = cameraWorldPos_;
        constants.groundLevelY = parameters_.groundLevelY;
        constants.multiScatteringFactor = parameters_.multiScatteringFactor;
        constants.apKmPerSlice = parameters_.apKmPerSlice;

        // ===== 月（第2大気ライト） =====
        constants.moonDirection = moonDirection_;
        constants.moonIntensity = moonIntensity_;
        constants.moonColor = { moonColor_.x, moonColor_.y, moonColor_.z };
        constants.moonDiskHalfAngleRad = parameters_.moonDiskAngularRadiusDeg * 3.14159265358979323846f / 180.0f;
        constants.moonDiskLuminanceScale = parameters_.moonDiskLuminanceScale;
        constants.hasMoon = hasMoonLight_ ? 1.0f : 0.0f;
        constants.moonPhaseFromSun = parameters_.moonPhaseFromSun ? 1.0f : 0.0f;

        // ===== 星空（SkyAtmosphere.PS が消費。LUT には影響しない） =====
        constants.starIntensity = parameters_.starIntensity;

        *constantData_ = constants;
    }
}
