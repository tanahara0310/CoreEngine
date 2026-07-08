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

namespace CoreEngine
{
    namespace {
        constexpr float kMetersToKm = 1.0f / 1000.0f;
        constexpr float kPerMeterToPerKm = 1000.0f;

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

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        descriptorManager_->CreateUAV(apResult_.Get(), uavDesc, cpuHandle, apResultUavHandle_, "AtmosphereAerialPerspectiveUAV");

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
            if (DirectionalLightData* sun = lightManager->GetAtmosphereSunLight()) {
                sunDirection_ = MathCore::Vector::Normalize(sun->direction);
                sunColor_ = sun->color;
                sunIntensity_ = sun->intensity;
                hasSunLight_ = sun->enabled;
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
        // 太陽方向・カメラ高度が変わった場合のみ Sky-View を再生成する
        // （Transmittance / Multi-Scattering は太陽方向に依存しないため再生成しない）
        constexpr float kDirEpsilon = 1e-5f;
        constexpr float kRadiusEpsilonMeters = 0.5f;
        const bool sunChanged =
            std::abs(sunDirection_.x - lastSunDirection_.x) > kDirEpsilon ||
            std::abs(sunDirection_.y - lastSunDirection_.y) > kDirEpsilon ||
            std::abs(sunDirection_.z - lastSunDirection_.z) > kDirEpsilon;
        const bool cameraChanged =
            std::abs(distanceFromPlanetCenter_ - lastCameraRadius_) > kRadiusEpsilonMeters;
        if (sunChanged || cameraChanged) {
            skyViewDirty_ = true;
            lastSunDirection_ = sunDirection_;
            lastCameraRadius_ = distanceFromPlanetCenter_;
        }

        // ===== 定数バッファ更新 =====
        UploadConstants();
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

        *constantData_ = constants;
    }
}
