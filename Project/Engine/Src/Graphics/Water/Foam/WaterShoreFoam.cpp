#include "pch.h"
#include "WaterShoreFoam.h"

#include <algorithm>
#include <iterator>
#include <string>

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UavTexture.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/ShaderBinder.h"
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

        /// @brief WaterShoreSeabedSmooth.CS.hlsl の契約
        namespace SeabedSmoothBind {
            enum Slot : size_t { gSeabedHeight, gSmoothSeabedOutput, WaterShoreSeabedSmoothConstants, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "gSeabedHeight",                   ShaderBindingType::SRV, BindingUsage::Required },
                { "gSmoothSeabedOutput",             ShaderBindingType::UAV, BindingUsage::Required },
                { "WaterShoreSeabedSmoothConstants", ShaderBindingType::CBV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        /// @brief WaterShoreSwash.CS.hlsl の契約
        namespace SwashBind {
            enum Slot : size_t { gSmoothSeabedHeight, gFFTOceanDisplacement, gSwashOutput, WaterShoreSwashConstants, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "gSmoothSeabedHeight",      ShaderBindingType::SRV, BindingUsage::Required },
                { "gFFTOceanDisplacement",    ShaderBindingType::SRV, BindingUsage::Required },
                { "gSwashOutput",             ShaderBindingType::UAV, BindingUsage::Required },
                { "WaterShoreSwashConstants", ShaderBindingType::CBV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }

        /// @brief WaterShoreFoam.CS.hlsl の契約
        namespace ShoreFoamBind {
            enum Slot : size_t {
                gSeabedHeight, gFFTOceanDisplacement, gShoreFoamPrev, gSwashDisplacement, gRunupFlow, gShoreFoamOutput,
                WaterShoreFoamConstants, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "gSeabedHeight",           ShaderBindingType::SRV, BindingUsage::Required },
                { "gFFTOceanDisplacement",   ShaderBindingType::SRV, BindingUsage::Required },
                { "gShoreFoamPrev",          ShaderBindingType::SRV, BindingUsage::Required },
                { "gSwashDisplacement",      ShaderBindingType::SRV, BindingUsage::Required },
                { "gRunupFlow",              ShaderBindingType::SRV, BindingUsage::Required },
                { "gShoreFoamOutput",        ShaderBindingType::UAV, BindingUsage::Required },
                { "WaterShoreFoamConstants", ShaderBindingType::CBV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
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

        if (!foamPipeline_.BuildCompute(
                dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, L"WaterShoreFoam.CS.hlsl")) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: failed to build shore foam compute pipeline.");
            return false;
        }
        if (!foamPipeline_.ResolveComputeBindings(ShoreFoamBind::kDecls, "WaterShoreFoam", foamBindings_)) {
            return false;
        }

        if (!smoothSeabedPipeline_.BuildCompute(
                dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, L"WaterShoreSeabedSmooth.CS.hlsl")) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: failed to build seabed smoothing compute pipeline.");
            return false;
        }
        if (!smoothSeabedPipeline_.ResolveComputeBindings(
                SeabedSmoothBind::kDecls, "WaterShoreSeabedSmooth", smoothSeabedBindings_)) {
            return false;
        }

        if (!swashPipeline_.BuildCompute(
                dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, L"WaterShoreSwash.CS.hlsl")) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreFoam: failed to build swash compute pipeline.");
            return false;
        }
        return swashPipeline_.ResolveComputeBindings(SwashBind::kDecls, "WaterShoreSwash", swashBindings_);
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
        const bool runupFlowEnabled = input.runupFlowSRV.ptr != 0 && input.runupFlowTexelsPerMeter > 0.0f;
        if (runupFlowEnabled) {
            constants.runupFlowOriginXZ[0] = input.runupFlowOriginXZ[0];
            constants.runupFlowOriginXZ[1] = input.runupFlowOriginXZ[1];
            constants.runupFlowTexelsPerMeter = input.runupFlowTexelsPerMeter;
            constants.runupFlowEnabled = 1u;
        }

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
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(smoothSeabedBindings_[SeabedSmoothBind::gSeabedHeight], input.seabedHeightSRV);
            binder.Set(smoothSeabedBindings_[SeabedSmoothBind::gSmoothSeabedOutput], smoothSeabed_.uav.gpuHandle);
            binder.Set(smoothSeabedBindings_[SeabedSmoothBind::WaterShoreSeabedSmoothConstants],
                dxCommon_->GetUploadRing().AllocateConstants(smoothConstants));
            binder.ValidateBeforeDraw(smoothSeabedBindings_);
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
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(swashBindings_[SwashBind::gSmoothSeabedHeight], smoothSeabed_.srv.gpuHandle);
            binder.Set(swashBindings_[SwashBind::gFFTOceanDisplacement], input.fftDisplacementSRV);
            binder.Set(swashBindings_[SwashBind::gSwashOutput], swash_.uav.gpuHandle);
            binder.Set(swashBindings_[SwashBind::WaterShoreSwashConstants],
                dxCommon_->GetUploadRing().AllocateConstants(swashConstants));
            binder.ValidateBeforeDraw(swashBindings_);
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

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(foamBindings_[ShoreFoamBind::gSeabedHeight], input.seabedHeightSRV);
        binder.Set(foamBindings_[ShoreFoamBind::gFFTOceanDisplacement], input.fftDisplacementSRV);
        binder.Set(foamBindings_[ShoreFoamBind::gShoreFoamPrev], readFoam.srv.gpuHandle);
        binder.Set(foamBindings_[ShoreFoamBind::gSwashDisplacement], swash_.srv.gpuHandle);
        // 浜を上がる膜のずれが無いときは、同じ形（x, z）の寄せ・引きのずれを結ぶ（シェーダーは読まない）
        binder.Set(foamBindings_[ShoreFoamBind::gRunupFlow],
            runupFlowEnabled ? input.runupFlowSRV : swash_.srv.gpuHandle);
        binder.Set(foamBindings_[ShoreFoamBind::gShoreFoamOutput], writeFoam.uav.gpuHandle);
        binder.Set(foamBindings_[ShoreFoamBind::WaterShoreFoamConstants],
            dxCommon_->GetUploadRing().AllocateConstants(constants));
        binder.ValidateBeforeDraw(foamBindings_);

        const UINT groups = DivRoundUp(kResolution, 8);
        cmdList->Dispatch(groups, groups, 1);

        Barrier::Transition(cmdList, writeFoam, kAnyShaderResource);
        foam_.Swap();
    }
}
