#include "pch.h"
#include "WaterShoreFoam.h"

#include <algorithm>
#include <string>

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Math/IntegerMath.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief フレーム間の時間の差の上限 [s]（シーンの切り替え直後などの大きな差を丸める）
        constexpr float kMaxDeltaSeconds = 0.1f;

        /// @brief 前のフレームから進んだシミュレーション時間 [s]（止まっている間は 0）
        float ComputeDeltaSeconds(float timeSeconds, float previousTimeSeconds)
        {
            return (std::clamp)(timeSeconds - previousTimeSeconds, 0.0f, kMaxDeltaSeconds);
        }
    }

    bool WaterShoreFoam::Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: initialization failed. dxCommon={} descriptorAllocator={}",
                dxCommon_ != nullptr, descriptorAllocator_ != nullptr);
            return false;
        }

        if (!CreatePipelines() || !CreateResources()) {
            return false;
        }
        isInitialized_ = true;
        return true;
    }

    void WaterShoreFoam::SetSettings(const Settings& settings)
    {
        if (!settings_.enabled && settings.enabled) {
            resetPending_ = true;
        }
        settings_ = settings;
    }

    bool WaterShoreFoam::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool foamBuilt = foamPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, foamShaderProvider_);
        if (!foamBuilt || !foamPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: failed to build shore foam compute pipeline.");
            return false;
        }

        const bool smoothBuilt = smoothSeabedPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, smoothSeabedShaderProvider_);
        if (!smoothBuilt || !smoothSeabedPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: failed to build seabed smoothing compute pipeline.");
            return false;
        }

        const bool swashBuilt = swashPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, swashShaderProvider_);
        if (!swashBuilt || !swashPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: failed to build swash compute pipeline.");
            return false;
        }
        return true;
    }

    bool WaterShoreFoam::CreateResources()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        // (被覆率, 寄せ・引きのずれ x, z, 0)
        const D3D12_RESOURCE_DESC desc = UavTexture::MakeDesc(kResolution, kResolution, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
        for (FFTOceanGpuTexture& texture : foam_) {
            if (!UavTexture::CreateResource(device, texture, desc)) {
                return false;
            }
        }
        for (size_t i = 0; i < foam_.size(); ++i) {
            const std::string idx = std::to_string(i);
            UavTexture::EnsureViews(*descriptorAllocator_, foam_[i].Get(), desc,
                foam_[i].srv, "WaterShoreFoamSRV_" + idx, foam_[i].uav, "WaterShoreFoamUAV_" + idx);
        }

        // ならした海底の高さと、寄せ・引きのずれ（x, z）
        if (!UavTexture::Create(device, *descriptorAllocator_, smoothSeabed_,
                UavTexture::MakeDesc(kSmoothSeabedResolution, kSmoothSeabedResolution, 1, DXGI_FORMAT_R32_FLOAT),
                smoothSeabed_.srv, "WaterShoreSmoothSeabedSRV", smoothSeabed_.uav, "WaterShoreSmoothSeabedUAV")
            || !UavTexture::Create(device, *descriptorAllocator_, swash_,
                UavTexture::MakeDesc(kSmoothSeabedResolution, kSmoothSeabedResolution, 1, DXGI_FORMAT_R32G32_FLOAT),
                swash_.srv, "WaterShoreSwashSRV", swash_.uav, "WaterShoreSwashUAV")) {
            return false;
        }

        resetPending_ = true;
        return true;
    }

    void WaterShoreFoam::Dispatch(ID3D12GraphicsCommandList* cmdList, const Input& input)
    {
        if (!isInitialized_ || !cmdList || input.seabedHeightSRV.ptr == 0 || input.seabedResolution == 0
            || input.fftDisplacementSRV.ptr == 0) {
            return;
        }
        if (input.frameNumber == lastFrameNumber_) {
            return;
        }
        lastFrameNumber_ = input.frameNumber;

        if (input.spectrumRevision != spectrumRevision_) {
            spectrumRevision_ = input.spectrumRevision;
            resetPending_ = true;
        }

        if (!settings_.enabled) {
            previousTimeSeconds_ = input.timeSeconds;
            return;
        }

        const float deltaSeconds = ComputeDeltaSeconds(input.timeSeconds, previousTimeSeconds_);
        previousTimeSeconds_ = input.timeSeconds;

        FoamConstants constants{};
        constants.windowOriginXZ[0] = input.windowOriginXZ[0];
        constants.windowOriginXZ[1] = input.windowOriginXZ[1];
        constants.prevWindowOriginXZ[0] = prevWindowOriginXZ_[0];
        constants.prevWindowOriginXZ[1] = prevWindowOriginXZ_[1];
        constants.windowSize = input.windowSize;
        constants.texelSize = kTexelSize;
        constants.resolution = kResolution;
        constants.waterRestHeight = input.waterRestHeight;
        constants.deltaSeconds = deltaSeconds;
        constants.decaySeconds = settings_.decaySeconds;
        constants.resetFoam = resetPending_ ? 1u : 0u;
        resetPending_ = false;
        for (int i = 0; i < 3; ++i) {
            constants.waveGroupPhase[i] = input.waveGroupPhase[i];
        }
        prevWindowOriginXZ_[0] = input.windowOriginXZ[0];
        prevWindowOriginXZ_[1] = input.windowOriginXZ[1];

        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        // 海底の高さをならす
        {
            SmoothSeabedConstants smoothConstants{};
            smoothConstants.outputResolution = kSmoothSeabedResolution;
            smoothConstants.inputResolution = input.seabedResolution;

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
                    static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(smoothConstants));
            }
            const UINT smoothGroups = DivRoundUp(kSmoothSeabedResolution, 8);
            cmdList->Dispatch(smoothGroups, smoothGroups, 1);
            Barrier::Transition(cmdList, smoothSeabed_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // 寄せ・引きのずれ
        {
            SwashConstants swashConstants{};
            swashConstants.windowOriginXZ[0] = input.windowOriginXZ[0];
            swashConstants.windowOriginXZ[1] = input.windowOriginXZ[1];
            swashConstants.windowSize = input.windowSize;
            swashConstants.resolution = kSmoothSeabedResolution;
            swashConstants.waterRestHeight = input.waterRestHeight;
            for (int i = 0; i < 3; ++i) {
                swashConstants.waveGroupPhase[i] = input.waveGroupPhase[i];
            }

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
                    static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(swashConstants));
            }
            const UINT swashGroups = DivRoundUp(kSmoothSeabedResolution, 8);
            cmdList->Dispatch(swashGroups, swashGroups, 1);
            Barrier::Transition(cmdList, swash_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        FFTOceanGpuTexture& writeFoam = foam_.Write();
        FFTOceanGpuTexture& readFoam = foam_.Read();
        Barrier::Transition(cmdList, writeFoam, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, readFoam, kAnyShaderResource);

        cmdList->SetPipelineState(foamPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(foamPipeline_.GetComputeRootSignature());

        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gSeabedHeight"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.seabedHeightSRV);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gFFTOceanDisplacement"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.fftDisplacementSRV);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gShoreFoamPrev"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), readFoam.srv.gpuHandle);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gSwashDisplacement"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), swash_.srv.gpuHandle);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gShoreFoamOutput"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), writeFoam.uav.gpuHandle);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("WaterShoreFoamConstants"); slot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(constants));
        }

        const UINT groups = DivRoundUp(kResolution, 8);
        cmdList->Dispatch(groups, groups, 1);

        Barrier::Transition(cmdList, writeFoam, kAnyShaderResource);
        foam_.Swap();
    }
}
