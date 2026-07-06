#include "pch.h"
#include "FFTOceanManager.h"

#include <algorithm>
#include <cmath>

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Water/FFTOceanDispatchHelper.h"
#include "Graphics/Water/FFTOceanManagerLogHelper.h"
#include "Graphics/Water/FFTOceanReadbackHelper.h"
#include "Graphics/Water/FFTOceanResourceFactory.h"
#include "Graphics/Water/FFTOceanSpectrumDebugHelper.h"
#include "Graphics/Water/FFTOceanSpectrumBuilder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        constexpr UINT Align256(UINT value)
        {
            return (value + 255) & ~255;
        }

    }

    bool FFTOceanManager::Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager)
    {
        // 外部依存を保持し、設定をGPU向けに正規化してから初期化を開始する。
        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        SanitizeSettings(settings_);

        if (!dxCommon_ || !descriptorManager_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: initialization failed. dxCommon={} descriptorManager={}",
                dxCommon_ != nullptr,
                descriptorManager_ != nullptr);
            return false;
        }

        // 実行に必要なパイプライン/リソースを順番に構築する。
        if (!CreatePipelines()
            || !CreateOutputTextures()
            || !CreateIntermediateTextures()
            || !CreateDebugReadbackBuffers()
            || !CreateSpectrumBuffer()
            || !CreateSimulationConstantBuffer()
            || !CreateIFFTConstantBuffer()) {
            return false;
        }

        // 初期スペクトルと定数を作成して、初回Dispatch可能な状態へ揃える。
        BuildSpectrum();
        UpdateSimulationConstants(0.0f);
        UpdateIFFTConstants(0, true, 1.0f);
        isInitialized_ = true;

        FFTOceanManagerLogHelper::LogInitialize(
            settings_.resolution,
            settings_.patchLength,
            settings_.windSpeed);
        return true;
    }

    void FFTOceanManager::SetSettings(const Settings& settings)
    {
        // 外部入力を安全なレンジへ補正する。
        Settings sanitized = settings;
        SanitizeSettings(sanitized);

        // 解像度変更はリソース再生成が必要なため現時点では固定。
        sanitized.resolution = settings_.resolution;

        const bool settingsChanged =
            sanitized.patchLength != settings_.patchLength ||
            sanitized.amplitudeScale != settings_.amplitudeScale ||
            sanitized.windDirection[0] != settings_.windDirection[0] ||
            sanitized.windDirection[1] != settings_.windDirection[1] ||
            sanitized.windSpeed != settings_.windSpeed ||
            sanitized.choppiness != settings_.choppiness ||
            sanitized.activeComponentCount != settings_.activeComponentCount ||
            sanitized.gravity != settings_.gravity;

        if (!settingsChanged) {
            return;
        }

        // GPU参照中のリソース更新を避けるため、フレーム完了を待機する。
        if (dxCommon_) {
            dxCommon_->WaitForPreviousFrame();
        }

        settings_ = sanitized;

        BuildSpectrum();
        UpdateSimulationConstants(mappedSimulationConstants_ ? mappedSimulationConstants_->timeSeconds : 0.0f);

        FFTOceanManagerLogHelper::LogSettingsUpdated(
            settings_.patchLength,
            settings_.amplitudeScale,
            settings_.windDirection[0],
            settings_.windDirection[1],
            settings_.windSpeed,
            settings_.choppiness,
            settings_.activeComponentCount,
            settings_.gravity);
    }

    void FFTOceanManager::Dispatch(ID3D12GraphicsCommandList* cmdList, float timeSeconds)
    {
        if (!isInitialized_ || !cmdList) {
            return;
        }

        if (!evolutionPipeline_.HasComputePSO() || !ifftPipeline_.HasComputePSO() || !finalizePipeline_.HasComputePSO()) {
            return;
        }

        // フレーム時刻とデバッグReadbackの状態を先頭で更新する。
        UpdateSimulationConstants(timeSeconds);
        ifftConstantsWriteIndex_ = 0;
        LogPendingIFFTDebugReadback();
        LogPendingEvolutionDebugReadback();
        LogPendingDebugReadback();

        static uint32_t sDispatchLogCounter = 0;
        if ((sDispatchLogCounter++ % 120) == 0 && mappedSimulationConstants_) {
            static float sPreviousLoggedTime = 0.0f;
            const float loggedDelta = timeSeconds - sPreviousLoggedTime;
            sPreviousLoggedTime = timeSeconds;

            FFTOceanManagerLogHelper::LogDispatchSummary(
                timeSeconds,
                loggedDelta,
                mappedSimulationConstants_->timeSeconds,
                mappedSimulationConstants_->amplitudeScale,
                settings_.windSpeed,
                mappedSimulationConstants_->choppiness,
                mappedSimulationConstants_->activeComponentCount);

            if (mappedSpectrumSamples_) {
                const uint32_t probeX = settings_.resolution / 2 + 1;
                const uint32_t probeY = settings_.resolution / 2;
                const uint32_t probeIndex = probeY * settings_.resolution + probeX;
                const SpectrumSample& probeSample = mappedSpectrumSamples_[probeIndex];
                const FFTOceanSpectrumDebugHelper::ComplexValue probeHeight =
                    FFTOceanSpectrumDebugHelper::EvaluateSpectrumSample(
                        probeSample.h0,
                        probeSample.h0Minus,
                        probeSample.angularFrequency,
                        probeSample.directionalWeight,
                        timeSeconds,
                        mappedSimulationConstants_->amplitudeScale,
                        mappedSimulationConstants_->activeComponentCount);

                FFTOceanManagerLogHelper::LogProbeSpectrum(
                    probeIndex,
                    probeSample.waveVector[0],
                    probeSample.waveVector[1],
                    probeSample.angularFrequency,
                    probeHeight.real,
                    probeHeight.imag,
                    FFTOceanSpectrumDebugHelper::ComputeMagnitude(probeHeight),
                    probeSample.directionalWeight);
            }
        }

        // 出力先をUAVに遷移して各Computeパスを実行する。
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, jacobianTexture_.Get(), jacobianState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ID3D12DescriptorHeap* descriptorHeaps[] = { descriptorManager_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        // スペクトル時間発展 -> IFFT(2系統) -> 最終合成 の順で波面を生成する。
        DispatchEvolutionPass(cmdList);

        static uint32_t sEvolutionReadbackCounter = 0;
        if ((sEvolutionReadbackCounter++ % 120) == 0) {
            ScheduleEvolutionDebugReadback(cmdList);
        }

        const uint32_t finalSpectrumAIndex = DispatchIFFTForTexture(
            cmdList,
            spectrumTextureA_,
            spectrumAState_,
            spectrumASrvHandle_,
            spectrumAUavHandle_,
            0);

        const uint32_t finalSpectrumBIndex = DispatchIFFTForTexture(
            cmdList,
            spectrumTextureB_,
            spectrumBState_,
            spectrumBSrvHandle_,
            spectrumBUavHandle_,
            0);

        static uint32_t sIfftLogCounter = 0;
        if ((sIfftLogCounter++ % 120) == 0) {
            FFTOceanManagerLogHelper::LogIFFTCompleted(
                finalSpectrumAIndex,
                finalSpectrumBIndex,
                settings_.resolution,
                GetLog2Resolution());

            ScheduleIFFTDebugReadback(
                cmdList,
                spectrumTextureA_[finalSpectrumAIndex].Get(),
                spectrumAState_[finalSpectrumAIndex],
                spectrumTextureB_[finalSpectrumBIndex].Get(),
                spectrumBState_[finalSpectrumBIndex]);
        }

        DispatchFinalizePass(
            cmdList,
            spectrumTextureA_[finalSpectrumAIndex].Get(),
            spectrumAState_[finalSpectrumAIndex],
            spectrumASrvHandle_[finalSpectrumAIndex],
            spectrumTextureB_[finalSpectrumBIndex].Get(),
            spectrumBState_[finalSpectrumBIndex],
            spectrumBSrvHandle_[finalSpectrumBIndex]);

        // 後段シェーダー参照用にSRV状態へ戻す。
        ResourceBarrierHelper::UAV(cmdList, displacementTexture_.Get());
        ResourceBarrierHelper::UAV(cmdList, normalTexture_.Get());
        ResourceBarrierHelper::UAV(cmdList, jacobianTexture_.Get());
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, jacobianTexture_.Get(), jacobianState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        static uint32_t sDebugReadbackCounter = 0;
        if ((sDebugReadbackCounter++ % 120) == 0) {
            ScheduleDebugReadback(cmdList);
        }
    }

    bool FFTOceanManager::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool evolutionBuilt = evolutionPipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            timeEvolutionShaderProvider_);
        const bool ifftBuilt = ifftPipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            ifftShaderProvider_);
        const bool finalizeBuilt = finalizePipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            finalizeShaderProvider_);

        if (!evolutionBuilt || !evolutionPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build time evolution compute pipeline.");
            return false;
        }

        if (!ifftBuilt || !ifftPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build IFFT compute pipeline.");
            return false;
        }

        if (!finalizeBuilt || !finalizePipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build finalize compute pipeline.");
            return false;
        }

        return true;
    }

    bool FFTOceanManager::CreateOutputTextures()
    {
        return FFTOceanResourceFactory::CreateOutputTextures(
            dxCommon_->GetDevice(),
            descriptorManager_,
            settings_.resolution,
            displacementTexture_,
            normalTexture_,
            jacobianTexture_,
            displacementSrvCpuHandle_,
            displacementSrvHandle_,
            displacementUavCpuHandle_,
            displacementUavHandle_,
            normalSrvCpuHandle_,
            normalSrvHandle_,
            normalUavCpuHandle_,
            normalUavHandle_,
            jacobianSrvCpuHandle_,
            jacobianSrvHandle_,
            jacobianUavCpuHandle_,
            jacobianUavHandle_,
            displacementState_,
            normalState_,
            jacobianState_);
    }

    bool FFTOceanManager::CreateDebugReadbackBuffers()
    {
        if (!dxCommon_ || !dxCommon_->GetDevice() || !displacementTexture_ || !normalTexture_) {
            return false;
        }

        const FFTOceanReadbackTextureCreateRequest requests[] = {
            { dxCommon_->GetDevice(), displacementTexture_.Get(), "displacement", &displacementReadbackBuffer_, &displacementReadbackLayout_, &displacementReadbackBytes_ },
            { dxCommon_->GetDevice(), normalTexture_.Get(), "normal", &normalReadbackBuffer_, &normalReadbackLayout_, &normalReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureA_[0].Get(), "evolutionA", &evolutionAReadbackBuffer_, &evolutionAReadbackLayout_, &evolutionAReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureB_[0].Get(), "evolutionB", &evolutionBReadbackBuffer_, &evolutionBReadbackLayout_, &evolutionBReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureA_[0].Get(), "ifftA", &ifftAReadbackBuffer_, &ifftAReadbackLayout_, &ifftAReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureB_[0].Get(), "ifftB", &ifftBReadbackBuffer_, &ifftBReadbackLayout_, &ifftBReadbackBytes_ },
        };

        for (const FFTOceanReadbackTextureCreateRequest& request : requests) {
            if (!FFTOceanReadbackHelper::CreateTextureReadbackBuffer(request)) {
                return false;
            }
        }

        return true;
    }

    bool FFTOceanManager::CreateIntermediateTextures()
    {
        return FFTOceanResourceFactory::CreateIntermediateTextures(
            dxCommon_->GetDevice(),
            descriptorManager_,
            settings_.resolution,
            spectrumTextureA_,
            spectrumTextureB_,
            spectrumASrvCpuHandle_,
            spectrumASrvHandle_,
            spectrumAUavCpuHandle_,
            spectrumAUavHandle_,
            spectrumBSrvCpuHandle_,
            spectrumBSrvHandle_,
            spectrumBUavCpuHandle_,
            spectrumBUavHandle_,
            spectrumAState_,
            spectrumBState_);
    }

    bool FFTOceanManager::CreateSpectrumBuffer()
    {
        void* mappedSpectrumSamples = mappedSpectrumSamples_;
        const bool created = FFTOceanResourceFactory::CreateSpectrumBuffers(
            dxCommon_->GetDevice(),
            descriptorManager_,
            settings_.resolution,
            sizeof(SpectrumSample),
            spectrumBuffer_,
            spectrumUploadBuffer_,
            mappedSpectrumSamples,
            spectrumSrvCpuHandle_,
            spectrumSrvHandle_,
            spectrumBufferState_,
            spectrumBufferDirty_);
        mappedSpectrumSamples_ = reinterpret_cast<SpectrumSample*>(mappedSpectrumSamples);
        return created;
    }

    bool FFTOceanManager::CreateSimulationConstantBuffer()
    {
        void* mappedSimulationConstants = mappedSimulationConstants_;
        const bool created = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            dxCommon_->GetDevice(),
            sizeof(SimulationConstants),
            simulationConstantsBuffer_,
            mappedSimulationConstants);
        mappedSimulationConstants_ = reinterpret_cast<SimulationConstants*>(mappedSimulationConstants);
        return created;
    }

    bool FFTOceanManager::CreateIFFTConstantBuffer()
    {
        return FFTOceanResourceFactory::CreateIFFTConstantBuffer(
            dxCommon_->GetDevice(),
            sizeof(IFFTConstants),
            kMaxIFFTPassCount,
            ifftConstantsBuffer_,
            mappedIFFTConstantsData_);
    }

    void FFTOceanManager::SanitizeSettings(Settings& settings) const
    {
        FFTOceanSpectrumBuilder::Settings builderSettings{};
        builderSettings.resolution = settings.resolution;
        builderSettings.patchLength = settings.patchLength;
        builderSettings.amplitudeScale = settings.amplitudeScale;
        builderSettings.windDirection[0] = settings.windDirection[0];
        builderSettings.windDirection[1] = settings.windDirection[1];
        builderSettings.windSpeed = settings.windSpeed;
        builderSettings.choppiness = settings.choppiness;
        builderSettings.activeComponentCount = settings.activeComponentCount;
        builderSettings.gravity = settings.gravity;

        FFTOceanSpectrumBuilder::SanitizeSettings(builderSettings, kMaxSpectrumComponents);

        settings.resolution = builderSettings.resolution;
        settings.patchLength = builderSettings.patchLength;
        settings.amplitudeScale = builderSettings.amplitudeScale;
        settings.windDirection[0] = builderSettings.windDirection[0];
        settings.windDirection[1] = builderSettings.windDirection[1];
        settings.windSpeed = builderSettings.windSpeed;
        settings.choppiness = builderSettings.choppiness;
        settings.activeComponentCount = builderSettings.activeComponentCount;
        settings.gravity = builderSettings.gravity;
    }

    void FFTOceanManager::BuildSpectrum()
    {
        if (!mappedSpectrumSamples_) {
            return;
        }

        // 現在設定からPhillipsスペクトルを再生成し、次DispatchでGPUへ反映する。
        const uint32_t resolution = settings_.resolution;
        const uint32_t sampleCount = resolution * resolution;
        FFTOceanSpectrumBuilder::Settings builderSettings{};
        builderSettings.resolution = settings_.resolution;
        builderSettings.patchLength = settings_.patchLength;
        builderSettings.amplitudeScale = settings_.amplitudeScale;
        builderSettings.windDirection[0] = settings_.windDirection[0];
        builderSettings.windDirection[1] = settings_.windDirection[1];
        builderSettings.windSpeed = settings_.windSpeed;
        builderSettings.choppiness = settings_.choppiness;
        builderSettings.activeComponentCount = settings_.activeComponentCount;
        builderSettings.gravity = settings_.gravity;

        FFTOceanSpectrumBuilder::BuildStats stats = FFTOceanSpectrumBuilder::BuildSpectrum(
            builderSettings,
            reinterpret_cast<FFTOceanSpectrumBuilder::SpectrumSample*>(mappedSpectrumSamples_),
            static_cast<size_t>(sampleCount));

        spectrumBufferDirty_ = true;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: BuildSpectrum stats resolution={} activeSamples={} avgAmp={:.6f} maxAmp={:.6f} maxOmega={:.6f} probeIndex={} probeH0=({:.6f}, {:.6f}) probeH0Minus=({:.6f}, {:.6f}) probeK=({:.4f}, {:.4f}) probeOmega={:.6f}",
            resolution,
            stats.activeSpectrumSampleCount,
            stats.averageSpectralAmplitude,
            stats.maxSpectralAmplitude,
            stats.maxAngularFrequency,
            stats.probeIndex,
            stats.probeSample.h0[0],
            stats.probeSample.h0[1],
            stats.probeSample.h0Minus[0],
            stats.probeSample.h0Minus[1],
            stats.probeSample.waveVector[0],
            stats.probeSample.waveVector[1],
            stats.probeSample.angularFrequency);
    }

    void FFTOceanManager::UpdateSimulationConstants(float timeSeconds)
    {
        if (!mappedSimulationConstants_) {
            return;
        }

        // 時刻と設定値をCS共通定数へ転送する。
        mappedSimulationConstants_->resolution = settings_.resolution;
        mappedSimulationConstants_->activeComponentCount = (std::min)(settings_.activeComponentCount, kMaxSpectrumComponents);
        mappedSimulationConstants_->patchLength = settings_.patchLength;
        mappedSimulationConstants_->timeSeconds = timeSeconds;
        mappedSimulationConstants_->choppiness = settings_.choppiness;
        mappedSimulationConstants_->gravity = settings_.gravity;
        mappedSimulationConstants_->amplitudeScale = settings_.amplitudeScale;
    }

    D3D12_GPU_VIRTUAL_ADDRESS FFTOceanManager::UpdateIFFTConstants(uint32_t stageIndex, bool isHorizontal, float normalizationScale)
    {
        if (!mappedIFFTConstantsData_ || !ifftConstantsBuffer_) {
            return 0;
        }

        if (ifftConstantsWriteIndex_ >= kMaxIFFTPassCount) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: IFFT constant slot overflow. writeIndex={} maxPassCount={}",
                ifftConstantsWriteIndex_,
                kMaxIFFTPassCount);
            ifftConstantsWriteIndex_ = kMaxIFFTPassCount - 1;
        }

        // IFFTの1パス分定数をリングバッファ上に書き込み、GPUアドレスを返す。
        const UINT slotSize = Align256(sizeof(IFFTConstants));
        IFFTConstants* constants = reinterpret_cast<IFFTConstants*>(
            mappedIFFTConstantsData_ + static_cast<size_t>(slotSize) * ifftConstantsWriteIndex_);
        constants->resolution = settings_.resolution;
        constants->stageIndex = stageIndex;
        constants->isHorizontal = isHorizontal ? 1 : 0;
        constants->normalizationScale = normalizationScale;

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress =
            ifftConstantsBuffer_->GetGPUVirtualAddress() + static_cast<UINT64>(slotSize) * ifftConstantsWriteIndex_;
        ++ifftConstantsWriteIndex_;
        return gpuAddress;
    }

    void FFTOceanManager::DispatchEvolutionPass(ID3D12GraphicsCommandList* cmdList)
    {
        if (spectrumBufferDirty_) {
            // スペクトルが更新された直後のみアップロードログを出力する。
            spectrumBufferDirty_ = false;
            FFTOceanManagerLogHelper::LogSpectrumUpload(spectrumUploadBuffer_->GetDesc().Width);
        }

        FFTOceanDispatchHelper::DispatchEvolutionPass(
            cmdList,
            spectrumTextureA_,
            spectrumAState_,
            spectrumTextureB_,
            spectrumBState_,
            evolutionPipeline_,
            spectrumSrvHandle_,
            spectrumAUavHandle_[0],
            spectrumBUavHandle_[0],
            simulationConstantsBuffer_.Get(),
            settings_.resolution);
    }

    void FFTOceanManager::DispatchIFFTPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* inputResource,
        D3D12_RESOURCE_STATES& inputState,
        CustomShaderPipeline& pipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputState,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUav,
        uint32_t stageIndex,
        bool isHorizontal,
        float normalizationScale)
    {
        const D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress =
            UpdateIFFTConstants(stageIndex, isHorizontal, normalizationScale);

        FFTOceanDispatchHelper::DispatchIFFTPass(
            cmdList,
            inputResource,
            inputState,
            pipeline,
            inputSrv,
            outputResource,
            outputState,
            outputUav,
            ifftConstantsGpuAddress,
            settings_.resolution);
    }

    uint32_t FFTOceanManager::DispatchIFFTForTexture(
        ID3D12GraphicsCommandList* cmdList,
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount>& resources,
        std::array<D3D12_RESOURCE_STATES, kPingPongCount>& resourceStates,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& srvHandles,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& uavHandles,
        uint32_t initialIndex)
    {
        uint32_t readIndex = initialIndex;
        uint32_t writeIndex = (initialIndex + 1) % kPingPongCount;
        const uint32_t log2Resolution = GetLog2Resolution();

        // 横方向IFFT。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = 1.0f;
            DispatchIFFTPass(
                cmdList,
                resources[readIndex].Get(),
                resourceStates[readIndex],
                ifftPipeline_,
                srvHandles[readIndex],
                resources[writeIndex].Get(),
                resourceStates[writeIndex],
                uavHandles[writeIndex],
                stageIndex,
                true,
                normalizationScale);
            std::swap(readIndex, writeIndex);
        }

        // 縦方向IFFT。最終ステージのみ1/N正規化を適用する。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = (stageIndex + 1 == log2Resolution)
                ? 1.0f / static_cast<float>(settings_.resolution)
                : 1.0f;
            DispatchIFFTPass(
                cmdList,
                resources[readIndex].Get(),
                resourceStates[readIndex],
                ifftPipeline_,
                srvHandles[readIndex],
                resources[writeIndex].Get(),
                resourceStates[writeIndex],
                uavHandles[writeIndex],
                stageIndex,
                false,
                normalizationScale);
            std::swap(readIndex, writeIndex);
        }

        return readIndex;
    }

    void FFTOceanManager::DispatchFinalizePass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* spectrumAResource,
        D3D12_RESOURCE_STATES& spectrumAState,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumASrv,
        ID3D12Resource* spectrumBResource,
        D3D12_RESOURCE_STATES& spectrumBState,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumBSrv)
    {
        FFTOceanDispatchHelper::DispatchFinalizePass(
            cmdList,
            spectrumAResource,
            spectrumAState,
            spectrumASrv,
            spectrumBResource,
            spectrumBState,
            spectrumBSrv,
            finalizePipeline_,
            displacementUavHandle_,
            normalUavHandle_,
            jacobianUavHandle_,
            simulationConstantsBuffer_.Get(),
            settings_.resolution);
    }

    void FFTOceanManager::LogPendingDebugReadback()
    {
        if (!debugReadbackPending_ || !displacementReadbackBuffer_ || !normalReadbackBuffer_) {
            return;
        }

        const FFTOceanReadbackHelper::SurfaceReadbackRequest request{
            displacementReadbackBuffer_.Get(),
            normalReadbackBuffer_.Get(),
            displacementReadbackBytes_,
            normalReadbackBytes_,
            displacementReadbackLayout_,
            normalReadbackLayout_,
            settings_.resolution,
            debugReadbackSequence_ };

        if (FFTOceanReadbackHelper::TryLogSurfaceReadback(request)) {
            debugReadbackPending_ = false;
        }
    }

    void FFTOceanManager::LogPendingEvolutionDebugReadback()
    {
        if (!evolutionDebugReadbackPending_ || !evolutionAReadbackBuffer_ || !evolutionBReadbackBuffer_) {
            return;
        }

        const FFTOceanReadbackHelper::SpectrumReadbackRequest request{
            evolutionAReadbackBuffer_.Get(),
            evolutionBReadbackBuffer_.Get(),
            evolutionAReadbackBytes_,
            evolutionBReadbackBytes_,
            evolutionAReadbackLayout_,
            evolutionBReadbackLayout_,
            settings_.resolution,
            evolutionDebugReadbackSequence_,
            false };

        if (FFTOceanReadbackHelper::TryLogSpectrumReadback(request)) {
            evolutionDebugReadbackPending_ = false;
        }
    }

    void FFTOceanManager::LogPendingIFFTDebugReadback()
    {
        if (!ifftDebugReadbackPending_ || !ifftAReadbackBuffer_ || !ifftBReadbackBuffer_) {
            return;
        }

        const FFTOceanReadbackHelper::SpectrumReadbackRequest request{
            ifftAReadbackBuffer_.Get(),
            ifftBReadbackBuffer_.Get(),
            ifftAReadbackBytes_,
            ifftBReadbackBytes_,
            ifftAReadbackLayout_,
            ifftBReadbackLayout_,
            settings_.resolution,
            ifftDebugReadbackSequence_,
            true };

        if (FFTOceanReadbackHelper::TryLogSpectrumReadback(request)) {
            ifftDebugReadbackPending_ = false;
        }
    }

    void FFTOceanManager::ScheduleIFFTDebugReadback(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* spectrumAResource,
        D3D12_RESOURCE_STATES& spectrumAState,
        ID3D12Resource* spectrumBResource,
        D3D12_RESOURCE_STATES& spectrumBState)
    {
        if (!cmdList || !ifftAReadbackBuffer_ || !ifftBReadbackBuffer_ || !spectrumAResource || !spectrumBResource) {
            return;
        }

        // IFFT完了後のスペクトルをReadbackバッファへコピーして次フレームで解析する。
        D3D12_TEXTURE_COPY_LOCATION srcA{};
        srcA.pResource = spectrumAResource;
        srcA.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcA.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstA{};
        dstA.pResource = ifftAReadbackBuffer_.Get();
        dstA.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstA.PlacedFootprint = ifftAReadbackLayout_;

        D3D12_TEXTURE_COPY_LOCATION srcB{};
        srcB.pResource = spectrumBResource;
        srcB.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcB.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstB{};
        dstB.pResource = ifftBReadbackBuffer_.Get();
        dstB.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstB.PlacedFootprint = ifftBReadbackLayout_;

        ResourceBarrierHelper::Transition(cmdList, spectrumAResource, spectrumAState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumBResource, spectrumBState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dstA, 0, 0, 0, &srcA, nullptr);
        cmdList->CopyTextureRegion(&dstB, 0, 0, 0, &srcB, nullptr);
        ResourceBarrierHelper::Transition(cmdList, spectrumAResource, spectrumAState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumBResource, spectrumBState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ifftDebugReadbackPending_ = true;
        ++ifftDebugReadbackSequence_;
    }

    void FFTOceanManager::ScheduleEvolutionDebugReadback(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !evolutionAReadbackBuffer_ || !evolutionBReadbackBuffer_) {
            return;
        }

        // 時間発展直後の中間スペクトルをReadbackする。
        D3D12_TEXTURE_COPY_LOCATION srcA{};
        srcA.pResource = spectrumTextureA_[0].Get();
        srcA.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcA.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstA{};
        dstA.pResource = evolutionAReadbackBuffer_.Get();
        dstA.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstA.PlacedFootprint = evolutionAReadbackLayout_;

        D3D12_TEXTURE_COPY_LOCATION srcB{};
        srcB.pResource = spectrumTextureB_[0].Get();
        srcB.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcB.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstB{};
        dstB.pResource = evolutionBReadbackBuffer_.Get();
        dstB.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstB.PlacedFootprint = evolutionBReadbackLayout_;

        ResourceBarrierHelper::Transition(cmdList, spectrumTextureA_[0].Get(), spectrumAState_[0], D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumTextureB_[0].Get(), spectrumBState_[0], D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dstA, 0, 0, 0, &srcA, nullptr);
        cmdList->CopyTextureRegion(&dstB, 0, 0, 0, &srcB, nullptr);
        ResourceBarrierHelper::Transition(cmdList, spectrumTextureA_[0].Get(), spectrumAState_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, spectrumTextureB_[0].Get(), spectrumBState_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        evolutionDebugReadbackPending_ = true;
        ++evolutionDebugReadbackSequence_;
    }

    void FFTOceanManager::ScheduleDebugReadback(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !displacementReadbackBuffer_ || !normalReadbackBuffer_) {
            return;
        }

        // 最終出力(変位/法線)をReadbackして統計ログに利用する。
        D3D12_TEXTURE_COPY_LOCATION displacementSrc{};
        displacementSrc.pResource = displacementTexture_.Get();
        displacementSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        displacementSrc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION displacementDst{};
        displacementDst.pResource = displacementReadbackBuffer_.Get();
        displacementDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        displacementDst.PlacedFootprint = displacementReadbackLayout_;

        D3D12_TEXTURE_COPY_LOCATION normalSrc{};
        normalSrc.pResource = normalTexture_.Get();
        normalSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        normalSrc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION normalDst{};
        normalDst.pResource = normalReadbackBuffer_.Get();
        normalDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        normalDst.PlacedFootprint = normalReadbackLayout_;

        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&displacementDst, 0, 0, 0, &displacementSrc, nullptr);
        cmdList->CopyTextureRegion(&normalDst, 0, 0, 0, &normalSrc, nullptr);
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        debugReadbackPending_ = true;
        ++debugReadbackSequence_;
    }

    uint32_t FFTOceanManager::GetLog2Resolution() const
    {
        uint32_t log2Resolution = 0;
        uint32_t resolution = settings_.resolution;
        while (resolution > 1) {
            resolution >>= 1;
            ++log2Resolution;
        }
        return log2Resolution;
    }
}
