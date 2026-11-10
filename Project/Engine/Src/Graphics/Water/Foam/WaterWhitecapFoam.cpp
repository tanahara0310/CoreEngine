#include "pch.h"
#include "WaterWhitecapFoam.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    static_assert(WaterWhitecapFoam::kLevelCount == 2, "FFTOceanFoamAccumulate.CS と WaterFrameConstants は 2 段を前提にする");

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

    bool WaterWhitecapFoam::Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterWhitecapFoam: initialization failed. dxCommon={} descriptorAllocator={}",
                dxCommon_ != nullptr, descriptorAllocator_ != nullptr);
            return false;
        }

        if (!CreatePipelines() || !CreateFoamResources() || !CreateStatisticsResources()) {
            return false;
        }
        logBreakingRatio_ = std::log(kInitialBreakingRatio);
        isInitialized_ = true;
        return true;
    }

    void WaterWhitecapFoam::SetSettings(const Settings& settings)
    {
        if (!settings_.enabled && settings.enabled) {
            RequestReset();
        }
        if (settings.whitecapScale != settings_.whitecapScale) {
            RequestReset();
        }
        settings_ = settings;
    }

    void WaterWhitecapFoam::RequestReset()
    {
        resetPending_ = true;
        calibrated_ = false;
        measuredResidueCoverage_ = 0.0f;
        ++calibrationEpoch_;
    }

    std::array<float, 2> WaterWhitecapFoam::ComputeDriftOffset(float timeSeconds) const
    {
        const float distance = kWindDriftFactor * (std::max)(settings_.windSpeed, 0.0f) * timeSeconds;
        return { settings_.windDirection[0] * distance, settings_.windDirection[1] * distance };
    }

    void WaterWhitecapFoam::UpdateWindows(float cameraX, float cameraZ, float timeSeconds)
    {
        const std::array<float, 2> drift = ComputeDriftOffset(timeSeconds);
        const float foamX = cameraX - drift[0];
        const float foamZ = cameraZ - drift[1];
        const uint32_t level = frameIndex_ % kLevelCount;
        const float texelSize = kLevelTexelSizes[level];
        const float halfSize = 0.5f * texelSize * static_cast<float>(kResolution);
        levelOrigins_[level * 2 + 0] = std::floor((foamX - halfSize) / texelSize) * texelSize;
        levelOrigins_[level * 2 + 1] = std::floor((foamZ - halfSize) / texelSize) * texelSize;
    }

    std::array<float, WaterWhitecapFoam::kLevelCount> WaterWhitecapFoam::GetResidueInvSizes() const
    {
        std::array<float, kLevelCount> invSizes{};
        for (uint32_t level = 0; level < kLevelCount; ++level) {
            invSizes[level] = 1.0f / (kLevelTexelSizes[level] * static_cast<float>(kResolution));
        }
        return invSizes;
    }

    std::array<float, 2> WaterWhitecapFoam::ComputeStretchAxis() const
    {
        const float t = (std::clamp)(
            (settings_.windSpeed - kStretchStartWindSpeed) / (kStretchFullWindSpeed - kStretchStartWindSpeed), 0.0f, 1.0f);
        const float stretch = 1.0f + (kMaxFoamStretch - 1.0f) * t * t * (3.0f - 2.0f * t);
        const float axisLength = std::sqrt(1.0f - 1.0f / stretch);
        return { settings_.windDirection[0] * axisLength, settings_.windDirection[1] * axisLength };
    }

    float WaterWhitecapFoam::GetTargetCoverage() const
    {
        const float windSpeed = settings_.windSpeed;
        if (windSpeed < kMinWhitecapWindSpeed) {
            return 0.0f;
        }
        const float coverage = settings_.whitecapScale * kMonahanCoefficient * std::pow(windSpeed, kMonahanExponent);
        return (std::clamp)(coverage, 0.0f, kMaxTargetCoverage);
    }

    bool WaterWhitecapFoam::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool foamBuilt = foamPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, foamShaderProvider_);
        if (!foamBuilt || !foamPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterWhitecapFoam: failed to build whitecap foam compute pipeline.");
            return false;
        }

        const bool statisticsBuilt = statisticsPipeline_.Build(
            dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, statisticsShaderProvider_);
        if (!statisticsBuilt || !statisticsPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterWhitecapFoam: failed to build whitecap statistics compute pipeline.");
            return false;
        }
        return true;
    }

    bool WaterWhitecapFoam::CreateFoamResources()
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        // 被覆率 1 チャンネルなので R16_FLOAT
        const DXGI_FORMAT format = DXGI_FORMAT_R16_FLOAT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kResolution;
        desc.Height = kResolution;
        desc.DepthOrArraySize = static_cast<UINT16>(kLevelCount);
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            for (FFTOceanGpuTexture& texture : foam_) {
                texture.Reset(
                    ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }
        catch (const std::exception&) {
            return false;
        }

        for (size_t i = 0; i < foam_.size(); ++i) {
            const std::string idx = std::to_string(i);

            D3D12_SHADER_RESOURCE_VIEW_DESC s{};
            s.Format = format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2DArray.MostDetailedMip = 0;
            s.Texture2DArray.MipLevels = 1;
            s.Texture2DArray.FirstArraySlice = 0;
            s.Texture2DArray.ArraySize = kLevelCount;
            foam_[i].srv = descriptorAllocator_->CreateSRV(foam_[i].Get(), s, "WaterWhitecapFoamSRV_" + idx);

            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = format;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            u.Texture2DArray.MipSlice = 0;
            u.Texture2DArray.FirstArraySlice = 0;
            u.Texture2DArray.ArraySize = kLevelCount;
            foam_[i].uav = descriptorAllocator_->CreateUAV(foam_[i].Get(), u, "WaterWhitecapFoamUAV_" + idx);
        }

        resetPending_ = true;
        return true;
    }

    bool WaterWhitecapFoam::CreateStatisticsResources()
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
        histogram_.assign(kHistogramBins, 0u);
        return true;
    }

    void WaterWhitecapFoam::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
        float timeSeconds,
        uint32_t spectrumRevision,
        const std::array<float, 3>& waveGroupPhase)
    {
        if (!isInitialized_ || !cmdList || jacobianSRV.ptr == 0) {
            return;
        }

        // 波面が別物になったフレームは蓄積を捨てる
        if (spectrumRevision != spectrumRevision_) {
            spectrumRevision_ = spectrumRevision;
            RequestReset();
        }

        // 無効の間はパスを飛ばす（水面のシェーダーも gFoamEnabled = 0 で読まない）
        if (!settings_.enabled) {
            previousTimeSeconds_ = timeSeconds;
            return;
        }

        const float deltaSeconds = ComputeDeltaSeconds(timeSeconds, previousTimeSeconds_);
        previousTimeSeconds_ = timeSeconds;
        elapsedSeconds_ += deltaSeconds;
        for (float& pending : pendingSeconds_) {
            pending += deltaSeconds;
        }

        // このフレームに進める段（UpdateWindows が角を動かした段）
        const uint32_t activeLevel = frameIndex_ % kLevelCount;

        FoamConstants constants{};
        for (size_t i = 0; i < levelOrigins_.size(); ++i) {
            constants.levelOriginsXZ[i] = levelOrigins_[i];
            constants.prevLevelOriginsXZ[i] = prevLevelOrigins_[i];
        }
        prevLevelOrigins_ = levelOrigins_;
        constants.deltaSeconds = pendingSeconds_[activeLevel];
        constants.activeLevel = activeLevel;
        pendingSeconds_[activeLevel] = 0.0f;
        if (resetPending_) {
            pendingSeconds_.fill(0.0f);
        }
        constants.foamBias = bias_;
        constants.foamGain = gain_;
        for (int c = 0; c < 3; ++c) {
            constants.cascadeWeights[c] = settings_.cascadeWeights[c];
            constants.waveGroupPhase[c] = waveGroupPhase[c];
        }
        constants.decaySeconds = settings_.decaySeconds;
        constants.resetFoam = resetPending_ ? 1u : 0u;
        resetPending_ = false;
        driftOffset_ = ComputeDriftOffset(timeSeconds);
        constants.driftOffsetXZ[0] = driftOffset_[0];
        constants.driftOffsetXZ[1] = driftOffset_[1];

        // 書き込み先はフレームの偶奇で決める。読む側は前のフレームの結果で、
        // 同じフレームの水面のピクセルシェーダーも読む
        const uint32_t writeIndex = frameIndex_ & 1u;
        const uint32_t readIndex = writeIndex ^ 1u;
        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Barrier::Transition(cmdList, foam_[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, foam_[readIndex], kAnyShaderResource);

        cmdList->SetPipelineState(foamPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(foamPipeline_.GetComputeRootSignature());

        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gJacobian"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), jacobianSRV);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gFoamPrev"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), foam_[readIndex].srv.gpuHandle);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("gFoamOutput"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), foam_[writeIndex].uav.gpuHandle);
        }
        if (const int slot = foamPipeline_.GetComputeRootParamIndex("FFTOceanFoamConstants"); slot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(constants));
        }

        const UINT groups = (kResolution + 7) / 8;
        cmdList->Dispatch(groups, groups, kLevelCount);

        Barrier::Transition(cmdList, foam_[writeIndex], kAnyShaderResource);
        ++frameIndex_;
    }

    bool WaterWhitecapFoam::ReadStatistics(uint32_t frameIndex)
    {
        if (!statisticsPending_[frameIndex]) {
            return false;
        }
        statisticsPending_[frameIndex] = false;
        if (statisticsEpoch_[frameIndex] != calibrationEpoch_) {
            return false;
        }

        // 同じ枠が回ってきた時点で、前にこの枠へ記録した GPU の処理は済んでいる
        uint32_t* words = nullptr;
        const D3D12_RANGE readRange{ 0, sizeof(uint32_t) * kStatisticsWords };
        if (FAILED(statisticsReadback_[frameIndex]->Map(0, &readRange, reinterpret_cast<void**>(&words)))) {
            return false;
        }
        std::memcpy(histogram_.data(), words, sizeof(uint32_t) * kHistogramBins);
        const uint32_t coverageSum = words[kHistogramBins];
        const uint32_t instantSum = words[kHistogramBins + 1];
        const uint32_t residueSum = words[kHistogramBins + 2];
        const D3D12_RANGE writtenRange{ 0, 0 };
        statisticsReadback_[frameIndex]->Unmap(0, &writtenRange);

        constexpr float kSampleCount =
            static_cast<float>(kStatisticsSampleResolution) * static_cast<float>(kStatisticsSampleResolution);
        constexpr float kSumToMean = 1.0f / (kCoverageFixedPointScale * kSampleCount);
        measuredCoverage_ = static_cast<float>(coverageSum) * kSumToMean;
        measuredInstantCoverage_ = static_cast<float>(instantSum) * kSumToMean;
        measuredResidueCoverage_ = static_cast<float>(residueSum) * kSumToMean;
        return true;
    }

    float WaterWhitecapFoam::FindHistogramQuantile(float fraction) const
    {
        uint64_t total = 0;
        for (const uint32_t count : histogram_) {
            total += count;
        }
        if (total == 0) {
            return bias_;
        }

        const double target = static_cast<double>(fraction) * static_cast<double>(total);
        const double binWidth = static_cast<double>(kHistogramMax - kHistogramMin) / kHistogramBins;
        double cumulative = 0.0;
        for (uint32_t i = 0; i < kHistogramBins; ++i) {
            const double count = histogram_[i];
            if (count > 0.0 && cumulative + count >= target) {
                // 段の中は一様に分布しているとみなして補間する
                const double t = (std::clamp)((target - cumulative) / count, 0.0, 1.0);
                return static_cast<float>(kHistogramMin + (i + t) * binWidth);
            }
            cumulative += count;
        }
        return kHistogramMax;
    }

    void WaterWhitecapFoam::UpdateCalibration()
    {
        const float elapsedSeconds = elapsedSeconds_;
        elapsedSeconds_ = 0.0f;

        const float target = GetTargetCoverage();
        if (target < kMinTargetCoverage) {
            bias_ = kNoWhitecapBias;
            calibrated_ = false;
            return;
        }

        // 測った被覆率を目標へ寄せる（シミュレーション時間が止まっている間は学ばない）
        if (calibrated_ && elapsedSeconds > 0.0f) {
            const float measured = (std::max)(measuredCoverage_, kMinTargetCoverage);
            const float error = (std::clamp)(std::log(target) - std::log(measured), -kMaxCalibrationError, kMaxCalibrationError);
            logBreakingRatio_ = (std::clamp)(
                logBreakingRatio_ + kCalibrationRate * elapsedSeconds * error,
                std::log(kMinBreakingRatio), std::log(kMaxBreakingRatio));
        }

        // 合成ヤコビアンの小さい方から fraction の点が砕け、そのうち kFullCoverageShare が被覆率 1 になる
        const float fraction = (std::min)(target * std::exp(logBreakingRatio_), kMaxBreakingFraction);
        const float bias = FindHistogramQuantile(fraction);
        const float fullCoverageBias = FindHistogramQuantile(fraction * kFullCoverageShare);
        const float gain = 1.0f / (std::max)(bias - fullCoverageBias, kMinRampWidth);

        if (!calibrated_) {
            bias_ = bias;
            gain_ = gain;
            calibrated_ = true;
        } else {
            const float blend = 1.0f - std::exp(-elapsedSeconds / kThresholdSmoothingSeconds);
            bias_ += (bias - bias_) * blend;
            gain_ += (gain - gain_) * blend;
        }

        if (++statisticsLogCounter_ >= kStatisticsLogInterval) {
            statisticsLogCounter_ = 0;
            Logger::GetInstance().Logf(LogLevel::Debug, LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterWhitecapFoam: whitecap coverage target={:.5f} measured={:.5f} ratio={:.3f} bias={:.4f} gain={:.2f}",
                target, measuredCoverage_, std::exp(logBreakingRatio_), bias_, gain_);
        }
    }

    void WaterWhitecapFoam::DispatchStatistics(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE jacobianSRV,
        uint32_t frameIndex,
        const std::array<float, 3>& waveGroupPhase)
    {
        if (!isInitialized_ || !cmdList || jacobianSRV.ptr == 0 || frameIndex >= kMaxFramesInFlight) {
            return;
        }
        if (ReadStatistics(frameIndex)) {
            UpdateCalibration();
        }
        if (!settings_.enabled) {
            return;
        }
        if (++statisticsFrameCounter_ < kStatisticsFrameInterval) {
            return;
        }
        statisticsFrameCounter_ = 0;

        // 標本は遠くの段と同じ範囲（参照格子座標へ戻した位置）から取る
        static_assert(kStatisticsSampleSpacing * kStatisticsSampleResolution == kLevelTexelSizes[1] * kResolution,
            "白波の統計の標本は遠くの段と同じ範囲を覆う");
        StatisticsConstants constants{};
        constants.sampleOriginXZ[0] = levelOrigins_[2] + driftOffset_[0];
        constants.sampleOriginXZ[1] = levelOrigins_[3] + driftOffset_[1];
        constants.sampleSpacing = kStatisticsSampleSpacing;
        constants.sampleResolution = kStatisticsSampleResolution;
        for (int c = 0; c < 3; ++c) {
            constants.cascadeWeights[c] = settings_.cascadeWeights[c];
        }
        constants.bias = bias_;
        constants.gain = gain_;
        constants.histogramMin = kHistogramMin;
        constants.histogramInvWidth = static_cast<float>(kHistogramBins) / (kHistogramMax - kHistogramMin);
        constants.histogramBins = kHistogramBins;
        for (size_t i = 0; i < waveGroupPhase.size(); ++i) {
            constants.waveGroupPhase[i] = waveGroupPhase[i];
        }
        constants.residueMeanCoverage = measuredResidueCoverage_;
        for (size_t i = 0; i < levelOrigins_.size(); ++i) {
            constants.residueOriginsXZ[i] = levelOrigins_[i];
        }
        const std::array<float, kLevelCount> invSizes = GetResidueInvSizes();
        constants.residueInvSizes[0] = invSizes[0];
        constants.residueInvSizes[1] = invSizes[1];
        constants.driftOffsetXZ[0] = driftOffset_[0];
        constants.driftOffsetXZ[1] = driftOffset_[1];

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
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), GetSRVHandle());
        }
        if (const int slot = statisticsPipeline_.GetComputeRootParamIndex("gStatistics"); slot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), statisticsUav_.gpuHandle);
        }
        if (const int slot = statisticsPipeline_.GetComputeRootParamIndex("WaterWhitecapStatisticsConstants"); slot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(constants));
        }
        const UINT groups = (kStatisticsSampleResolution + 7) / 8;
        cmdList->Dispatch(groups, groups, 1);

        // CPU は同じ枠が回ってきたフレームで読む。最後は次のフレームの 0 の写し込みに備えて写し先へ戻す
        Barrier::Transition(cmdList, statistics_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyBufferRegion(statisticsReadback_[frameIndex].Get(), 0, statistics_.Get(), 0, kBytes);
        Barrier::Transition(cmdList, statistics_, D3D12_RESOURCE_STATE_COPY_DEST);
        statisticsPending_[frameIndex] = true;
        statisticsEpoch_[frameIndex] = calibrationEpoch_;
    }
}
