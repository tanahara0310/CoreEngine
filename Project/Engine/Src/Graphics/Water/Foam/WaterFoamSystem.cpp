#include "pch.h"
#include "WaterFoamSystem.h"

#include <algorithm>
#include <string>

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Water/FFTOceanResourceFactory.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        constexpr UINT Align256(UINT value)
        {
            return (value + 255) & ~255;
        }

        /// @brief フレーム間の時間の差の上限 [s]（シーンの切り替え直後などの大きな差を丸める）
        constexpr float kMaxDeltaSeconds = 0.1f;

        /// @brief 前のフレームから進んだシミュレーション時間 [s]（止まっている間は 0）
        float ComputeDeltaSeconds(float timeSeconds, float previousTimeSeconds)
        {
            return (std::clamp)(timeSeconds - previousTimeSeconds, 0.0f, kMaxDeltaSeconds);
        }
    }

    bool WaterFoamSystem::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        uint32_t fftResolution)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        fftResolution_ = fftResolution;
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_ || fftResolution_ == 0) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: initialization failed. dxCommon={} descriptorAllocator={} fftResolution={}",
                dxCommon_ != nullptr, descriptorAllocator_ != nullptr, fftResolution_);
            return false;
        }

        if (!CreatePipelines() || !CreateWhitecapResources() || !CreateShoreResources()) {
            return false;
        }
        isInitialized_ = true;
        return true;
    }

    void WaterFoamSystem::SetSettings(const Settings& settings)
    {
        if (!settings_.enabled && settings.enabled) {
            whitecapResetPending_ = true;
            shoreResetPending_ = true;
        }
        settings_ = settings;
    }

    bool WaterFoamSystem::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool whitecapBuilt = whitecapPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, whitecapShaderProvider_);
        if (!whitecapBuilt || !whitecapPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: failed to build whitecap foam compute pipeline.");
            return false;
        }

        const bool shoreBuilt = shorePipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, shoreShaderProvider_);
        if (!shoreBuilt || !shorePipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: failed to build shore foam compute pipeline.");
            return false;
        }

        const bool smoothBuilt = smoothSeabedPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, smoothSeabedShaderProvider_);
        if (!smoothBuilt || !smoothSeabedPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: failed to build seabed smoothing compute pipeline.");
            return false;
        }

        const bool swashBuilt = swashPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, swashShaderProvider_);
        if (!swashBuilt || !swashPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: failed to build swash compute pipeline.");
            return false;
        }
        return true;
    }

    bool WaterFoamSystem::CreateWhitecapResources()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        // 被覆率 1 チャンネルなので R16_FLOAT
        const DXGI_FORMAT format = DXGI_FORMAT_R16_FLOAT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = fftResolution_;
        desc.Height = fftResolution_;
        desc.DepthOrArraySize = static_cast<UINT16>(kCascadeCount);
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            for (FFTOceanGpuTexture& texture : whitecap_) {
                texture.Reset(
                    ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }
        catch (const std::exception&) {
            return false;
        }

        for (size_t i = 0; i < whitecap_.size(); ++i) {
            const std::string idx = std::to_string(i);

            D3D12_SHADER_RESOURCE_VIEW_DESC s{};
            s.Format = format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2DArray.MostDetailedMip = 0;
            s.Texture2DArray.MipLevels = 1;
            s.Texture2DArray.FirstArraySlice = 0;
            s.Texture2DArray.ArraySize = kCascadeCount;
            whitecap_[i].srv = descriptorAllocator_->CreateSRV(whitecap_[i].Get(), s, "WaterWhitecapFoamSRV_" + idx);

            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = format;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            u.Texture2DArray.MipSlice = 0;
            u.Texture2DArray.FirstArraySlice = 0;
            u.Texture2DArray.ArraySize = kCascadeCount;
            whitecap_[i].uav = descriptorAllocator_->CreateUAV(whitecap_[i].Get(), u, "WaterWhitecapFoamUAV_" + idx);
        }

        // 定数は 1 枠を毎フレーム上書きする
        void* mapped = nullptr;
        const bool created = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            device.Get(), Align256(sizeof(WhitecapConstants)), whitecapConstantsBuffer_, mapped);
        mappedWhitecapConstants_ = static_cast<WhitecapConstants*>(mapped);

        whitecapResetPending_ = true;
        return created && mappedWhitecapConstants_;
    }

    bool WaterFoamSystem::CreateShoreResources()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        // (被覆率, 寄せ・引きのずれ x, z, 0)
        const DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kShoreResolution;
        desc.Height = kShoreResolution;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            for (FFTOceanGpuTexture& texture : shore_) {
                texture.Reset(
                    ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }
        catch (const std::exception&) {
            return false;
        }

        for (size_t i = 0; i < shore_.size(); ++i) {
            const std::string idx = std::to_string(i);

            D3D12_SHADER_RESOURCE_VIEW_DESC s{};
            s.Format = format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2D.MipLevels = 1;
            shore_[i].srv = descriptorAllocator_->CreateSRV(shore_[i].Get(), s, "WaterShoreFoamSRV_" + idx);

            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = format;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            shore_[i].uav = descriptorAllocator_->CreateUAV(shore_[i].Get(), u, "WaterShoreFoamUAV_" + idx);
        }

        void* mapped = nullptr;
        const bool created = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            device.Get(), Align256(sizeof(ShoreConstants)), shoreConstantsBuffer_, mapped);
        mappedShoreConstants_ = static_cast<ShoreConstants*>(mapped);

        // ならした海底の高さ
        const DXGI_FORMAT smoothFormat = DXGI_FORMAT_R32_FLOAT;
        D3D12_RESOURCE_DESC smoothDesc = desc;
        smoothDesc.Width = kSmoothSeabedResolution;
        smoothDesc.Height = kSmoothSeabedResolution;
        smoothDesc.Format = smoothFormat;
        try {
            smoothSeabed_.Reset(
                ResourceFactory::CreateTextureResource(device, smoothDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC smoothSrv{};
        smoothSrv.Format = smoothFormat;
        smoothSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        smoothSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        smoothSrv.Texture2D.MipLevels = 1;
        smoothSeabed_.srv = descriptorAllocator_->CreateSRV(smoothSeabed_.Get(), smoothSrv, "WaterShoreSmoothSeabedSRV");
        D3D12_UNORDERED_ACCESS_VIEW_DESC smoothUav{};
        smoothUav.Format = smoothFormat;
        smoothUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        smoothSeabed_.uav = descriptorAllocator_->CreateUAV(smoothSeabed_.Get(), smoothUav, "WaterShoreSmoothSeabedUAV");

        void* mappedSmooth = nullptr;
        const bool smoothCreated = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            device.Get(), Align256(sizeof(SmoothSeabedConstants)), smoothSeabedConstantsBuffer_, mappedSmooth);
        mappedSmoothSeabedConstants_ = static_cast<SmoothSeabedConstants*>(mappedSmooth);

        // 寄せ・引きのずれ（x, z）
        const DXGI_FORMAT swashFormat = DXGI_FORMAT_R32G32_FLOAT;
        D3D12_RESOURCE_DESC swashDesc = smoothDesc;
        swashDesc.Format = swashFormat;
        try {
            swash_.Reset(
                ResourceFactory::CreateTextureResource(device, swashDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC swashSrv{};
        swashSrv.Format = swashFormat;
        swashSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        swashSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        swashSrv.Texture2D.MipLevels = 1;
        swash_.srv = descriptorAllocator_->CreateSRV(swash_.Get(), swashSrv, "WaterShoreSwashSRV");
        D3D12_UNORDERED_ACCESS_VIEW_DESC swashUav{};
        swashUav.Format = swashFormat;
        swashUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        swash_.uav = descriptorAllocator_->CreateUAV(swash_.Get(), swashUav, "WaterShoreSwashUAV");

        void* mappedSwash = nullptr;
        const bool swashCreated = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            device.Get(), Align256(sizeof(SwashConstants)), swashConstantsBuffer_, mappedSwash);
        mappedSwashConstants_ = static_cast<SwashConstants*>(mappedSwash);

        shoreResetPending_ = true;
        return created && mappedShoreConstants_ && smoothCreated && mappedSmoothSeabedConstants_
            && swashCreated && mappedSwashConstants_;
    }

    void WaterFoamSystem::DispatchWhitecap(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
        float timeSeconds,
        uint32_t spectrumRevision)
    {
        if (!isInitialized_ || !cmdList || jacobianSRV.ptr == 0) {
            return;
        }

        // 波面が別物になったフレームは蓄積を捨てる
        if (spectrumRevision != whitecapSpectrumRevision_) {
            whitecapSpectrumRevision_ = spectrumRevision;
            whitecapResetPending_ = true;
        }

        // 無効の間はパスを飛ばす（水面のシェーダーも gFoamEnabled = 0 で読まない）
        if (!settings_.enabled) {
            whitecapPreviousTimeSeconds_ = timeSeconds;
            return;
        }

        const float deltaSeconds = ComputeDeltaSeconds(timeSeconds, whitecapPreviousTimeSeconds_);
        whitecapPreviousTimeSeconds_ = timeSeconds;

        WhitecapConstants& constants = *mappedWhitecapConstants_;
        constants.resolution = fftResolution_;
        constants.deltaSeconds = deltaSeconds;
        constants.foamBias = settings_.bias;
        constants.foamGain = settings_.gain;
        for (int c = 0; c < 3; ++c) {
            constants.cascadeWeights[c] = settings_.cascadeWeights[c];
        }
        constants.decaySeconds = settings_.decaySeconds;
        constants.resetFoam = whitecapResetPending_ ? 1u : 0u;
        whitecapResetPending_ = false;

        // 書き込み先はフレームの偶奇で決める。読む側は前のフレームの結果で、
        // 同じフレームの水面のピクセルシェーダーも読む
        const uint32_t writeIndex = whitecapFrameIndex_ & 1u;
        const uint32_t readIndex = writeIndex ^ 1u;
        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Barrier::Transition(cmdList, whitecap_[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, whitecap_[readIndex], kAnyShaderResource);

        cmdList->SetPipelineState(whitecapPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(whitecapPipeline_.GetComputeRootSignature());

        if (const int slot = whitecapPipeline_.GetComputeRootParamIndex("gJacobian"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), jacobianSRV);
        }
        if (const int slot = whitecapPipeline_.GetComputeRootParamIndex("gFoamPrev"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), whitecap_[readIndex].srv.gpuHandle);
        }
        if (const int slot = whitecapPipeline_.GetComputeRootParamIndex("gFoamOutput"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), whitecap_[writeIndex].uav.gpuHandle);
        }
        if (const int slot = whitecapPipeline_.GetComputeRootParamIndex("FFTOceanFoamConstants"); slot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(slot), whitecapConstantsBuffer_->GetGPUVirtualAddress());
        }

        const UINT groups = (fftResolution_ + 7) / 8;
        cmdList->Dispatch(groups, groups, kCascadeCount);

        Barrier::Transition(cmdList, whitecap_[writeIndex], kAnyShaderResource);
        ++whitecapFrameIndex_;
    }

    void WaterFoamSystem::DispatchShore(ID3D12GraphicsCommandList* cmdList, const ShoreInput& input)
    {
        if (!isInitialized_ || !cmdList || input.seabedHeightSRV.ptr == 0 || input.seabedResolution == 0
            || input.fftDisplacementSRV.ptr == 0) {
            return;
        }
        if (input.frameNumber == shoreLastFrameNumber_) {
            return;
        }
        shoreLastFrameNumber_ = input.frameNumber;

        if (input.spectrumRevision != shoreSpectrumRevision_) {
            shoreSpectrumRevision_ = input.spectrumRevision;
            shoreResetPending_ = true;
        }

        if (!settings_.enabled) {
            shorePreviousTimeSeconds_ = input.timeSeconds;
            return;
        }

        const float deltaSeconds = ComputeDeltaSeconds(input.timeSeconds, shorePreviousTimeSeconds_);
        shorePreviousTimeSeconds_ = input.timeSeconds;

        ShoreConstants& constants = *mappedShoreConstants_;
        constants.windowOriginXZ[0] = input.windowOriginXZ[0];
        constants.windowOriginXZ[1] = input.windowOriginXZ[1];
        constants.prevWindowOriginXZ[0] = shorePrevWindowOriginXZ_[0];
        constants.prevWindowOriginXZ[1] = shorePrevWindowOriginXZ_[1];
        constants.windowSize = input.windowSize;
        constants.texelSize = kShoreTexelSize;
        constants.resolution = kShoreResolution;
        constants.waterRestHeight = input.waterRestHeight;
        constants.deltaSeconds = deltaSeconds;
        constants.decaySeconds = settings_.decaySeconds;
        constants.resetFoam = shoreResetPending_ ? 1u : 0u;
        shoreResetPending_ = false;
        shorePrevWindowOriginXZ_[0] = input.windowOriginXZ[0];
        shorePrevWindowOriginXZ_[1] = input.windowOriginXZ[1];

        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        // 海底の高さをならす
        {
            mappedSmoothSeabedConstants_->outputResolution = kSmoothSeabedResolution;
            mappedSmoothSeabedConstants_->inputResolution = input.seabedResolution;

            Barrier::Transition(cmdList, smoothSeabed_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmdList->SetPipelineState(smoothSeabedPipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(smoothSeabedPipeline_.GetComputeRootSignature());
            if (const int slot = smoothSeabedPipeline_.GetComputeRootParamIndex("gSeabedHeight"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.seabedHeightSRV);
            }
            if (const int slot = smoothSeabedPipeline_.GetComputeRootParamIndex("gSmoothSeabedOutput"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), smoothSeabed_.uav.gpuHandle);
            }
            if (const int slot = smoothSeabedPipeline_.GetComputeRootParamIndex("WaterShoreSeabedSmoothConstants"); slot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(slot), smoothSeabedConstantsBuffer_->GetGPUVirtualAddress());
            }
            const UINT smoothGroups = (kSmoothSeabedResolution + 7) / 8;
            cmdList->Dispatch(smoothGroups, smoothGroups, 1);
            Barrier::Transition(cmdList, smoothSeabed_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // 寄せ・引きのずれ
        {
            SwashConstants& swashConstants = *mappedSwashConstants_;
            swashConstants.windowOriginXZ[0] = input.windowOriginXZ[0];
            swashConstants.windowOriginXZ[1] = input.windowOriginXZ[1];
            swashConstants.windowSize = input.windowSize;
            swashConstants.resolution = kSmoothSeabedResolution;
            swashConstants.waterRestHeight = input.waterRestHeight;

            Barrier::Transition(cmdList, swash_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmdList->SetPipelineState(swashPipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(swashPipeline_.GetComputeRootSignature());
            if (const int slot = swashPipeline_.GetComputeRootParamIndex("gSmoothSeabedHeight"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), smoothSeabed_.srv.gpuHandle);
            }
            if (const int slot = swashPipeline_.GetComputeRootParamIndex("gFFTOceanDisplacement"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.fftDisplacementSRV);
            }
            if (const int slot = swashPipeline_.GetComputeRootParamIndex("gSwashOutput"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), swash_.uav.gpuHandle);
            }
            if (const int slot = swashPipeline_.GetComputeRootParamIndex("WaterShoreSwashConstants"); slot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(slot), swashConstantsBuffer_->GetGPUVirtualAddress());
            }
            const UINT swashGroups = (kSmoothSeabedResolution + 7) / 8;
            cmdList->Dispatch(swashGroups, swashGroups, 1);
            Barrier::Transition(cmdList, swash_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        const uint32_t writeIndex = shoreFrameIndex_ & 1u;
        const uint32_t readIndex = writeIndex ^ 1u;
        Barrier::Transition(cmdList, shore_[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, shore_[readIndex], kAnyShaderResource);

        cmdList->SetPipelineState(shorePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(shorePipeline_.GetComputeRootSignature());

        if (const int slot = shorePipeline_.GetComputeRootParamIndex("gSeabedHeight"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.seabedHeightSRV);
        }
        if (const int slot = shorePipeline_.GetComputeRootParamIndex("gFFTOceanDisplacement"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.fftDisplacementSRV);
        }
        if (const int slot = shorePipeline_.GetComputeRootParamIndex("gShoreFoamPrev"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), shore_[readIndex].srv.gpuHandle);
        }
        if (const int slot = shorePipeline_.GetComputeRootParamIndex("gSwashDisplacement"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), swash_.srv.gpuHandle);
        }
        if (const int slot = shorePipeline_.GetComputeRootParamIndex("gShoreFoamOutput"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), shore_[writeIndex].uav.gpuHandle);
        }
        if (const int slot = shorePipeline_.GetComputeRootParamIndex("WaterShoreFoamConstants"); slot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(slot), shoreConstantsBuffer_->GetGPUVirtualAddress());
        }

        const UINT groups = (kShoreResolution + 7) / 8;
        cmdList->Dispatch(groups, groups, 1);

        Barrier::Transition(cmdList, shore_[writeIndex], kAnyShaderResource);
        ++shoreFrameIndex_;
    }
}
