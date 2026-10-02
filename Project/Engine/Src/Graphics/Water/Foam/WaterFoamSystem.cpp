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

        if (!CreatePipelines() || !CreateWhitecapResources()) {
            return false;
        }
        isInitialized_ = true;
        return true;
    }

    void WaterFoamSystem::SetSettings(const Settings& settings)
    {
        if (!settings_.enabled && settings.enabled) {
            whitecapResetPending_ = true;
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
}
