#include "pch.h"
#include "WaterFoamSystem.h"

#include <algorithm>
#include <cstring>
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

        if (!CreatePipelines() || !CreateWhitecapResources() || !CreateShoreResources()
            || !CreateStatisticsResources()) {
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

        const bool statisticsBuilt = statisticsPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, statisticsShaderProvider_);
        if (!statisticsBuilt || !statisticsPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: failed to build whitecap statistics compute pipeline.");
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

    bool WaterFoamSystem::CreateStatisticsResources()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        constexpr UINT64 kBytes = sizeof(uint32_t) * kStatisticsWords;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kBytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // 毎フレームの最初は 0 を写し込むので、写し先の状態で作る
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        if (FAILED(device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)))) {
            return false;
        }
        statistics_.Reset(std::move(buffer), D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = kStatisticsWords;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        statisticsUav_ = descriptorAllocator_->CreateUAV(statistics_.Get(), uavDesc, "WaterWhitecapStatisticsUAV");

        try {
            statisticsZero_ = ResourceFactory::CreateBufferResource(device, kBytes);
            void* zeroMapped = nullptr;
            if (FAILED(statisticsZero_->Map(0, nullptr, &zeroMapped))) {
                return false;
            }
            std::memset(zeroMapped, 0, static_cast<size_t>(kBytes));
            statisticsZero_->Unmap(0, nullptr);

            for (auto& readback : statisticsReadback_) {
                readback = ResourceFactory::CreateBufferResource(device, kBytes, D3D12_HEAP_TYPE_READBACK);
            }
        }
        catch (const std::exception&) {
            return false;
        }
        statisticsPending_.fill(false);
        whitecapHistogram_.assign(kHistogramBins, 0u);

        void* mapped = nullptr;
        const bool created = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            device.Get(), Align256(sizeof(StatisticsConstants)), statisticsConstantsBuffer_, mapped);
        mappedStatisticsConstants_ = static_cast<StatisticsConstants*>(mapped);
        return created && mappedStatisticsConstants_;
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

    void WaterFoamSystem::ReadWhitecapStatistics(uint32_t frameIndex)
    {
        if (!statisticsPending_[frameIndex]) {
            return;
        }
        statisticsPending_[frameIndex] = false;

        // 同じ枠が回ってきた時点で、前にこの枠へ記録した GPU の処理は済んでいる
        uint32_t* words = nullptr;
        const D3D12_RANGE readRange{ 0, sizeof(uint32_t) * kStatisticsWords };
        if (FAILED(statisticsReadback_[frameIndex]->Map(0, &readRange, reinterpret_cast<void**>(&words)))) {
            return;
        }
        std::memcpy(whitecapHistogram_.data(), words, sizeof(uint32_t) * kHistogramBins);
        const uint32_t coverageSum = words[kHistogramBins];
        const D3D12_RANGE writtenRange{ 0, 0 };
        statisticsReadback_[frameIndex]->Unmap(0, &writtenRange);

        constexpr float kSampleCount =
            static_cast<float>(kStatisticsSampleResolution) * static_cast<float>(kStatisticsSampleResolution);
        measuredWhitecapCoverage_ = static_cast<float>(coverageSum) / (kCoverageFixedPointScale * kSampleCount);

        if (++statisticsLogCounter_ >= kStatisticsLogInterval) {
            statisticsLogCounter_ = 0;
            Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterFoamSystem: whitecap coverage measured={:.5f} bias={:.4f} gain={:.3f}",
                measuredWhitecapCoverage_, settings_.bias, settings_.gain);
        }
    }

    void WaterFoamSystem::DispatchWhitecapStatistics(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
        uint32_t frameIndex)
    {
        if (!isInitialized_ || !cmdList || jacobianSRV.ptr == 0 || frameIndex >= kMaxFramesInFlight) {
            return;
        }
        ReadWhitecapStatistics(frameIndex);
        if (!settings_.enabled) {
            return;
        }

        StatisticsConstants& constants = *mappedStatisticsConstants_;
        constexpr float kSampleExtent = kStatisticsSampleSpacing * static_cast<float>(kStatisticsSampleResolution);
        constants.sampleOriginXZ[0] = -0.5f * kSampleExtent;
        constants.sampleOriginXZ[1] = -0.5f * kSampleExtent;
        constants.sampleSpacing = kStatisticsSampleSpacing;
        constants.sampleResolution = kStatisticsSampleResolution;
        for (int c = 0; c < 3; ++c) {
            constants.cascadeWeights[c] = settings_.cascadeWeights[c];
        }
        constants.bias = settings_.bias;
        constants.gain = settings_.gain;
        constants.histogramMin = kHistogramMin;
        constants.histogramInvWidth = static_cast<float>(kHistogramBins) / (kHistogramMax - kHistogramMin);
        constants.histogramBins = kHistogramBins;

        constexpr UINT64 kBytes = sizeof(uint32_t) * kStatisticsWords;
        Barrier::Transition(cmdList, statistics_, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(statistics_.Get(), 0, statisticsZero_.Get(), 0, kBytes);
        Barrier::Transition(cmdList, statistics_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(statisticsPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(statisticsPipeline_.GetComputeRootSignature());
        if (const int slot = statisticsPipeline_.GetComputeRootParamIndex("gJacobian"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), jacobianSRV);
        }
        if (const int slot = statisticsPipeline_.GetComputeRootParamIndex("gWhitecapFoam"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), GetWhitecapSRVHandle());
        }
        if (const int slot = statisticsPipeline_.GetComputeRootParamIndex("gStatistics"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), statisticsUav_.gpuHandle);
        }
        if (const int slot = statisticsPipeline_.GetComputeRootParamIndex("WaterWhitecapStatisticsConstants"); slot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(slot), statisticsConstantsBuffer_->GetGPUVirtualAddress());
        }
        const UINT groups = (kStatisticsSampleResolution + 7) / 8;
        cmdList->Dispatch(groups, groups, 1);

        // CPU は同じ枠が回ってきたフレームで読む。最後は次のフレームの 0 の写し込みに備えて写し先へ戻す
        Barrier::Transition(cmdList, statistics_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyBufferRegion(statisticsReadback_[frameIndex].Get(), 0, statistics_.Get(), 0, kBytes);
        Barrier::Transition(cmdList, statistics_, D3D12_RESOURCE_STATE_COPY_DEST);
        statisticsPending_[frameIndex] = true;
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
