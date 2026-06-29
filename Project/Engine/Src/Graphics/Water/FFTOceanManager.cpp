#include "pch.h"
#include "FFTOceanManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        constexpr UINT Align256(UINT value)
        {
            return (value + 255u) & ~255u;
        }

        constexpr float kPi = 3.14159265359f;
        constexpr float kTwoPi = 6.28318530718f;

        D3D12_RESOURCE_DESC MakeTexture2DDesc(uint32_t resolution, DXGI_FORMAT format)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = resolution;
            desc.Height = resolution;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }

        uint32_t MirrorCoord(uint32_t value, uint32_t resolution)
        {
            return (resolution - value) & (resolution - 1u);
        }

        struct ComplexDebugValue {
            float real = 0.0f;
            float imag = 0.0f;
        };

        float ComputeMagnitude(const ComplexDebugValue& value)
        {
            return std::sqrt(value.real * value.real + value.imag * value.imag);
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> CreateReadbackBuffer(ID3D12Device* device, UINT64 sizeInBytes)
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = Align256(static_cast<UINT>(sizeInBytes));
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            const HRESULT hr = device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&resource));
            if (FAILED(hr)) {
                return nullptr;
            }

            return resource;
        }

        float HalfToFloat(uint16_t value)
        {
            const uint32_t sign = (static_cast<uint32_t>(value) >> 15u) & 0x1u;
            const uint32_t exponent = (static_cast<uint32_t>(value) >> 10u) & 0x1Fu;
            const uint32_t mantissa = static_cast<uint32_t>(value) & 0x3FFu;

            uint32_t resultBits = 0u;
            if (exponent == 0u) {
                if (mantissa == 0u) {
                    resultBits = sign << 31u;
                } else {
                    uint32_t normalizedMantissa = mantissa;
                    uint32_t adjustedExponent = 113u;
                    while ((normalizedMantissa & 0x400u) == 0u) {
                        normalizedMantissa <<= 1u;
                        --adjustedExponent;
                    }
                    normalizedMantissa &= 0x3FFu;
                    resultBits = (sign << 31u) | (adjustedExponent << 23u) | (normalizedMantissa << 13u);
                }
            } else if (exponent == 0x1Fu) {
                resultBits = (sign << 31u) | 0x7F800000u | (mantissa << 13u);
            } else {
                const uint32_t adjustedExponent = exponent + (127u - 15u);
                resultBits = (sign << 31u) | (adjustedExponent << 23u) | (mantissa << 13u);
            }

            float result = 0.0f;
            std::memcpy(&result, &resultBits, sizeof(float));
            return result;
        }

        struct Float4DebugValue {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        Float4DebugValue LoadHalf4(const uint8_t* baseAddress, uint32_t x, uint32_t y, uint32_t rowPitch)
        {
            const uint8_t* texelAddress = baseAddress + static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * sizeof(uint16_t) * 4u;
            const uint16_t* halfValues = reinterpret_cast<const uint16_t*>(texelAddress);
            Float4DebugValue value{};
            value.x = HalfToFloat(halfValues[0]);
            value.y = HalfToFloat(halfValues[1]);
            value.z = HalfToFloat(halfValues[2]);
            value.w = HalfToFloat(halfValues[3]);
            return value;
        }

        Float4DebugValue LoadFloat4(const uint8_t* baseAddress, uint32_t x, uint32_t y, uint32_t rowPitch)
        {
            const uint8_t* texelAddress = baseAddress + static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * sizeof(float) * 4u;
            Float4DebugValue value{};
            std::memcpy(&value, texelAddress, sizeof(Float4DebugValue));
            return value;
        }

        ComplexDebugValue EvaluateSpectrumSample(
            const float* h0,
            const float* h0Minus,
            float angularFrequency,
            float directionalWeight,
            float timeSeconds,
            float amplitudeScale,
            uint32_t activeComponentCount)
        {
            const float angularPhase = angularFrequency * timeSeconds;
            const float cosPhase = std::cos(angularPhase);
            const float sinPhase = std::sin(angularPhase);

            ComplexDebugValue value{};
            value.real =
                (h0[0] * cosPhase - h0[1] * sinPhase)
                + (h0Minus[0] * cosPhase - h0Minus[1] * sinPhase);
            value.imag =
                (h0[0] * sinPhase + h0[1] * cosPhase)
                + (-h0Minus[0] * sinPhase + h0Minus[1] * cosPhase);

            const float bandLimit = (std::max)(static_cast<float>(activeComponentCount) / 64.0f, 1.0f / 64.0f);
            const float bandFade = (std::clamp)((bandLimit - directionalWeight) * 16.0f + 1.0f, 0.0f, 1.0f);
            value.real *= bandFade * amplitudeScale;
            value.imag *= bandFade * amplitudeScale;
            return value;
        }
    }

    bool FFTOceanManager::Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager)
    {
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

        if (!CreatePipelines()
            || !CreateOutputTextures()
            || !CreateIntermediateTextures()
            || !CreateDebugReadbackBuffers()
            || !CreateSpectrumBuffer()
            || !CreateSimulationConstantBuffer()
            || !CreateIFFTConstantBuffer()) {
            return false;
        }

        BuildSpectrum();
        UpdateSimulationConstants(0.0f);
        UpdateIFFTConstants(0u, true, 1.0f);
        isInitialized_ = true;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: initialized. resolution={} patchLength={:.2f} windSpeed={:.2f}",
            settings_.resolution,
            settings_.patchLength,
            settings_.windSpeed);
        return true;
    }

    void FFTOceanManager::SetSettings(const Settings& settings)
    {
        Settings sanitized = settings;
        SanitizeSettings(sanitized);

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

        if (dxCommon_) {
            dxCommon_->WaitForPreviousFrame();
        }

        settings_ = sanitized;

        BuildSpectrum();
        UpdateSimulationConstants(mappedSimulationConstants_ ? mappedSimulationConstants_->timeSeconds : 0.0f);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: settings updated. patchLength={:.2f} amplitudeScale={:.3f} windDir=({:.3f}, {:.3f}) windSpeed={:.3f} choppiness={:.3f} components={} gravity={:.3f}",
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

        UpdateSimulationConstants(timeSeconds);
        ifftConstantsWriteIndex_ = 0u;
        LogPendingIFFTDebugReadback();
        LogPendingEvolutionDebugReadback();
        LogPendingDebugReadback();

        static uint32_t sDispatchLogCounter = 0u;
        if ((sDispatchLogCounter++ % 120u) == 0u && mappedSimulationConstants_) {
            static float sPreviousLoggedTime = 0.0f;
            const float loggedDelta = timeSeconds - sPreviousLoggedTime;
            sPreviousLoggedTime = timeSeconds;

            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: dispatch time={:.3f} delta={:.3f} cb.time={:.3f} amp={:.3f} windSpeed={:.3f} choppiness={:.3f} components={}",
                timeSeconds,
                loggedDelta,
                mappedSimulationConstants_->timeSeconds,
                mappedSimulationConstants_->amplitudeScale,
                settings_.windSpeed,
                mappedSimulationConstants_->choppiness,
                mappedSimulationConstants_->activeComponentCount);

            if (mappedSpectrumSamples_) {
                const uint32_t probeX = settings_.resolution / 2u + 1u;
                const uint32_t probeY = settings_.resolution / 2u;
                const uint32_t probeIndex = probeY * settings_.resolution + probeX;
                const SpectrumSample& probeSample = mappedSpectrumSamples_[probeIndex];
                const ComplexDebugValue probeHeight = EvaluateSpectrumSample(
                    probeSample.h0,
                    probeSample.h0Minus,
                    probeSample.angularFrequency,
                    probeSample.directionalWeight,
                    timeSeconds,
                    mappedSimulationConstants_->amplitudeScale,
                    mappedSimulationConstants_->activeComponentCount);

                Logger::GetInstance().Infof(
                    LogCategory::Graphics,
                    LogSubCategory::Pipeline,
                    "FFTOceanManager: probeSpectrum index={} k=({:.4f}, {:.4f}) omega={:.4f} height=({:.6f}, {:.6f}) |h|={:.6f} dirWeight={:.4f}",
                    probeIndex,
                    probeSample.waveVector[0],
                    probeSample.waveVector[1],
                    probeSample.angularFrequency,
                    probeHeight.real,
                    probeHeight.imag,
                    ComputeMagnitude(probeHeight),
                    probeSample.directionalWeight);
            }
        }

        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ID3D12DescriptorHeap* descriptorHeaps[] = { descriptorManager_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        DispatchEvolutionPass(cmdList);

        static uint32_t sEvolutionReadbackCounter = 0u;
        if ((sEvolutionReadbackCounter++ % 120u) == 0u) {
            ScheduleEvolutionDebugReadback(cmdList);
        }

        const uint32_t finalSpectrumAIndex = DispatchIFFTForTexture(
            cmdList,
            spectrumTextureA_,
            spectrumAState_,
            spectrumASrvHandle_,
            spectrumAUavHandle_,
            0u);

        const uint32_t finalSpectrumBIndex = DispatchIFFTForTexture(
            cmdList,
            spectrumTextureB_,
            spectrumBState_,
            spectrumBSrvHandle_,
            spectrumBUavHandle_,
            0u);

        static uint32_t sIfftLogCounter = 0u;
        if ((sIfftLogCounter++ % 120u) == 0u) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: IFFT completed. finalSpectrumAIndex={} finalSpectrumBIndex={} resolution={} log2Resolution={}",
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

        ResourceBarrierHelper::UAV(cmdList, displacementTexture_.Get());
        ResourceBarrierHelper::UAV(cmdList, normalTexture_.Get());
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        static uint32_t sDebugReadbackCounter = 0u;
        if ((sDebugReadbackCounter++ % 120u) == 0u) {
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
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const D3D12_RESOURCE_DESC textureDesc = MakeTexture2DDesc(settings_.resolution, DXGI_FORMAT_R16G16B16A16_FLOAT);

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&displacementTexture_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::RenderTarget,
                "FFTOceanManager: displacement texture creation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&normalTexture_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::RenderTarget,
                "FFTOceanManager: normal texture creation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        descriptorManager_->CreateSRV(
            displacementTexture_.Get(),
            srvDesc,
            displacementSrvCpuHandle_,
            displacementSrvHandle_,
            "FFTOceanDisplacementSRV");
        descriptorManager_->CreateSRV(
            normalTexture_.Get(),
            srvDesc,
            normalSrvCpuHandle_,
            normalSrvHandle_,
            "FFTOceanNormalSRV");

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = textureDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        descriptorManager_->CreateUAV(
            displacementTexture_.Get(),
            uavDesc,
            displacementUavCpuHandle_,
            displacementUavHandle_,
            "FFTOceanDisplacementUAV");
        descriptorManager_->CreateUAV(
            normalTexture_.Get(),
            uavDesc,
            normalUavCpuHandle_,
            normalUavHandle_,
            "FFTOceanNormalUAV");

        displacementState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        normalState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    }

    bool FFTOceanManager::CreateDebugReadbackBuffers()
    {
        if (!dxCommon_ || !dxCommon_->GetDevice() || !displacementTexture_ || !normalTexture_) {
            return false;
        }

        auto createForTexture = [this](
            ID3D12Resource* texture,
            Microsoft::WRL::ComPtr<ID3D12Resource>& readbackBuffer,
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout,
            UINT64& totalBytes,
            const char* debugName) -> bool
        {
            UINT numRows = 0u;
            UINT64 rowSizeInBytes = 0u;
            const D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();
            dxCommon_->GetDevice()->GetCopyableFootprints(
                &textureDesc,
                0,
                1,
                0,
                &layout,
                &numRows,
                &rowSizeInBytes,
                &totalBytes);

            readbackBuffer = CreateReadbackBuffer(dxCommon_->GetDevice(), totalBytes);
            if (!readbackBuffer) {
                Logger::GetInstance().Errorf(
                    LogCategory::Graphics,
                    LogSubCategory::Buffer,
                    "FFTOceanManager: {} readback buffer creation failed. bytes={}",
                    debugName,
                    totalBytes);
                return false;
            }

            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: {} readback ready. rowPitch={} totalBytes={}",
                debugName,
                layout.Footprint.RowPitch,
                totalBytes);
            return true;
        };

        return createForTexture(
                displacementTexture_.Get(),
                displacementReadbackBuffer_,
                displacementReadbackLayout_,
                displacementReadbackBytes_,
                "displacement")
            && createForTexture(
                normalTexture_.Get(),
                normalReadbackBuffer_,
                normalReadbackLayout_,
                normalReadbackBytes_,
                "normal")
            && createForTexture(
                spectrumTextureA_[0].Get(),
                evolutionAReadbackBuffer_,
                evolutionAReadbackLayout_,
                evolutionAReadbackBytes_,
                "evolutionA")
            && createForTexture(
                spectrumTextureB_[0].Get(),
                evolutionBReadbackBuffer_,
                evolutionBReadbackLayout_,
                evolutionBReadbackBytes_,
                "evolutionB")
            && createForTexture(
                spectrumTextureA_[0].Get(),
                ifftAReadbackBuffer_,
                ifftAReadbackLayout_,
                ifftAReadbackBytes_,
                "ifftA")
            && createForTexture(
                spectrumTextureB_[0].Get(),
                ifftBReadbackBuffer_,
                ifftBReadbackLayout_,
                ifftBReadbackBytes_,
                "ifftB");
    }

    bool FFTOceanManager::CreateIntermediateTextures()
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const D3D12_RESOURCE_DESC textureDesc = MakeTexture2DDesc(settings_.resolution, DXGI_FORMAT_R32G32B32A32_FLOAT);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = textureDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        for (uint32_t i = 0; i < kPingPongCount; ++i) {
            HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&spectrumTextureA_[i]));
            if (FAILED(hr)) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::RenderTarget,
                    "FFTOceanManager: spectrum A texture creation failed. index={} hr={:#x}",
                    i,
                    static_cast<uint32_t>(hr));
                return false;
            }

            hr = dxCommon_->GetDevice()->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&spectrumTextureB_[i]));
            if (FAILED(hr)) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::RenderTarget,
                    "FFTOceanManager: spectrum B texture creation failed. index={} hr={:#x}",
                    i,
                    static_cast<uint32_t>(hr));
                return false;
            }

            descriptorManager_->CreateSRV(
                spectrumTextureA_[i].Get(),
                srvDesc,
                spectrumASrvCpuHandle_[i],
                spectrumASrvHandle_[i],
                std::string("FFTOceanSpectrumA_SRV_") + std::to_string(i));
            descriptorManager_->CreateUAV(
                spectrumTextureA_[i].Get(),
                uavDesc,
                spectrumAUavCpuHandle_[i],
                spectrumAUavHandle_[i],
                std::string("FFTOceanSpectrumA_UAV_") + std::to_string(i));

            descriptorManager_->CreateSRV(
                spectrumTextureB_[i].Get(),
                srvDesc,
                spectrumBSrvCpuHandle_[i],
                spectrumBSrvHandle_[i],
                std::string("FFTOceanSpectrumB_SRV_") + std::to_string(i));
            descriptorManager_->CreateUAV(
                spectrumTextureB_[i].Get(),
                uavDesc,
                spectrumBUavCpuHandle_[i],
                spectrumBUavHandle_[i],
                std::string("FFTOceanSpectrumB_UAV_") + std::to_string(i));

            spectrumAState_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            spectrumBState_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        return true;
    }

    bool FFTOceanManager::CreateSpectrumBuffer()
    {
        const uint64_t sampleCount = static_cast<uint64_t>(settings_.resolution) * static_cast<uint64_t>(settings_.resolution);
        const uint64_t bufferSize = sizeof(SpectrumSample) * sampleCount;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bufferSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES defaultHeapProps{};
        defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&spectrumBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: default spectrum buffer creation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_HEAP_PROPERTIES uploadHeapProps{};
        uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&spectrumUploadBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: upload spectrum buffer creation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        hr = spectrumUploadBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&mappedSpectrumSamples_));
        if (FAILED(hr) || !mappedSpectrumSamples_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: upload spectrum buffer map failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        std::memset(mappedSpectrumSamples_, 0, static_cast<size_t>(bufferSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = static_cast<UINT>(sampleCount);
        srvDesc.Buffer.StructureByteStride = sizeof(SpectrumSample);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        descriptorManager_->CreateSRV(
            spectrumUploadBuffer_.Get(),
            srvDesc,
            spectrumSrvCpuHandle_,
            spectrumSrvHandle_,
            "FFTOceanSpectrumSamplesSRV");

        spectrumBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        spectrumBufferDirty_ = true;

        return true;
    }

    bool FFTOceanManager::CreateSimulationConstantBuffer()
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = Align256(sizeof(SimulationConstants));
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&simulationConstantsBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: simulation constant buffer creation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        hr = simulationConstantsBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&mappedSimulationConstants_));
        if (FAILED(hr) || !mappedSimulationConstants_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: simulation constant buffer map failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        std::memset(mappedSimulationConstants_, 0, sizeof(SimulationConstants));
        return true;
    }

    bool FFTOceanManager::CreateIFFTConstantBuffer()
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(Align256(sizeof(IFFTConstants))) * kMaxIFFTPassCount;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&ifftConstantsBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: IFFT constant buffer creation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange{ 0, 0 };
        hr = ifftConstantsBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&mappedIFFTConstantsData_));
        if (FAILED(hr) || !mappedIFFTConstantsData_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Buffer,
                "FFTOceanManager: IFFT constant buffer map failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        std::memset(mappedIFFTConstantsData_, 0, static_cast<size_t>(desc.Width));
        return true;
    }

    void FFTOceanManager::SanitizeSettings(Settings& settings) const
    {
        settings.resolution = (std::clamp)(settings.resolution, 8u, 512u);
        uint32_t powerOfTwo = 8u;
        while (powerOfTwo < settings.resolution && powerOfTwo < 512u) {
            powerOfTwo <<= 1u;
        }
        settings.resolution = powerOfTwo;
        settings.patchLength = (std::max)(settings.patchLength, 1.0f);
        settings.amplitudeScale = (std::max)(settings.amplitudeScale, 0.0f);
        settings.windSpeed = (std::max)(settings.windSpeed, 0.0f);
        settings.choppiness = (std::max)(settings.choppiness, 0.0f);
        settings.activeComponentCount = (std::clamp)(settings.activeComponentCount, 1u, kMaxSpectrumComponents);
        settings.gravity = (std::max)(settings.gravity, 0.1f);

        const float windDirX = settings.windDirection[0];
        const float windDirY = settings.windDirection[1];
        const float windLength = std::sqrt(windDirX * windDirX + windDirY * windDirY);
        if (windLength <= 1.0e-4f) {
            settings.windDirection[0] = 1.0f;
            settings.windDirection[1] = 0.0f;
        } else {
            settings.windDirection[0] = windDirX / windLength;
            settings.windDirection[1] = windDirY / windLength;
        }
    }

    void FFTOceanManager::BuildSpectrum()
    {
        if (!mappedSpectrumSamples_) {
            return;
        }

        const uint32_t resolution = settings_.resolution;
        const uint32_t sampleCount = resolution * resolution;
        const float patchLength = settings_.patchLength;
        const float gravity = settings_.gravity;
        const float windSpeed = (std::max)(settings_.windSpeed, 0.1f);
        const float windX = settings_.windDirection[0];
        const float windY = settings_.windDirection[1];
        const float largestWave = (windSpeed * windSpeed) / gravity;
        const float dampingLength = largestWave * 0.001f;
        const float maxWaveNumber = kPi * static_cast<float>(resolution) / patchLength;

        struct TempSpectrumSample {
            float real = 0.0f;
            float imag = 0.0f;
            float waveVectorX = 0.0f;
            float waveVectorY = 0.0f;
            float angularFrequency = 0.0f;
            float normalizedBand = 0.0f;
        };

        std::vector<TempSpectrumSample> tempSpectrum(sampleCount);
        std::mt19937 rng(20260626u);
        std::normal_distribution<float> gaussianDistribution(0.0f, 1.0f);
        uint32_t activeSpectrumSampleCount = 0u;
        float maxSpectralAmplitude = 0.0f;
        float accumulatedSpectralAmplitude = 0.0f;
        float maxAngularFrequency = 0.0f;

        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const uint32_t index = y * resolution + x;
                const int32_t centeredX = static_cast<int32_t>(x) - static_cast<int32_t>(resolution / 2u);
                const int32_t centeredY = static_cast<int32_t>(y) - static_cast<int32_t>(resolution / 2u);
                const float waveVectorX = static_cast<float>(centeredX) * (kTwoPi / patchLength);
                const float waveVectorY = static_cast<float>(centeredY) * (kTwoPi / patchLength);
                const float waveNumberSquared = waveVectorX * waveVectorX + waveVectorY * waveVectorY;

                TempSpectrumSample& sample = tempSpectrum[index];
                sample.waveVectorX = waveVectorX;
                sample.waveVectorY = waveVectorY;

                if (waveNumberSquared <= 1.0e-8f) {
                    continue;
                }

                const float waveNumber = std::sqrt(waveNumberSquared);
                const float directionX = waveVectorX / waveNumber;
                const float directionY = waveVectorY / waveNumber;
                const float waveAlignment = directionX * windX + directionY * windY;
                const float largestWaveSquared = largestWave * largestWave;
                const float dampingSquared = dampingLength * dampingLength;
                float phillipsSpectrum = std::exp(-1.0f / (waveNumberSquared * largestWaveSquared));
                phillipsSpectrum /= waveNumberSquared * waveNumberSquared;
                phillipsSpectrum *= waveAlignment * waveAlignment;
                phillipsSpectrum *= std::exp(-waveNumberSquared * dampingSquared);
                if (waveAlignment < 0.0f) {
                    phillipsSpectrum *= 0.25f;
                }

                const float spectralAmplitude = std::sqrt((std::max)(phillipsSpectrum, 0.0f)) * 0.70710678f;
                sample.real = gaussianDistribution(rng) * spectralAmplitude;
                sample.imag = gaussianDistribution(rng) * spectralAmplitude;
                sample.angularFrequency = std::sqrt(gravity * waveNumber);
                sample.normalizedBand = (std::min)(waveNumber / (std::max)(maxWaveNumber, 1.0e-4f), 1.0f);

                if (spectralAmplitude > 0.0f) {
                    ++activeSpectrumSampleCount;
                    accumulatedSpectralAmplitude += spectralAmplitude;
                    maxSpectralAmplitude = (std::max)(maxSpectralAmplitude, spectralAmplitude);
                    maxAngularFrequency = (std::max)(maxAngularFrequency, sample.angularFrequency);
                }
            }
        }

        for (uint32_t y = 0; y < resolution; ++y) {
            for (uint32_t x = 0; x < resolution; ++x) {
                const uint32_t index = y * resolution + x;
                const uint32_t mirroredX = MirrorCoord(x, resolution);
                const uint32_t mirroredY = MirrorCoord(y, resolution);
                const uint32_t mirroredIndex = mirroredY * resolution + mirroredX;

                mappedSpectrumSamples_[index].h0[0] = tempSpectrum[index].real;
                mappedSpectrumSamples_[index].h0[1] = tempSpectrum[index].imag;
                mappedSpectrumSamples_[index].h0Minus[0] = tempSpectrum[mirroredIndex].real;
                mappedSpectrumSamples_[index].h0Minus[1] = -tempSpectrum[mirroredIndex].imag;
                mappedSpectrumSamples_[index].waveVector[0] = tempSpectrum[index].waveVectorX;
                mappedSpectrumSamples_[index].waveVector[1] = tempSpectrum[index].waveVectorY;
                mappedSpectrumSamples_[index].angularFrequency = tempSpectrum[index].angularFrequency;
                mappedSpectrumSamples_[index].directionalWeight = tempSpectrum[index].normalizedBand;
            }
        }

        spectrumBufferDirty_ = true;

        const uint32_t probeX = resolution / 2u + 1u;
        const uint32_t probeY = resolution / 2u;
        const uint32_t probeIndex = probeY * resolution + probeX;
        const SpectrumSample& probeSample = mappedSpectrumSamples_[probeIndex];
        const float averageSpectralAmplitude = activeSpectrumSampleCount > 0u
            ? accumulatedSpectralAmplitude / static_cast<float>(activeSpectrumSampleCount)
            : 0.0f;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: BuildSpectrum stats resolution={} activeSamples={} avgAmp={:.6f} maxAmp={:.6f} maxOmega={:.6f} probeIndex={} probeH0=({:.6f}, {:.6f}) probeH0Minus=({:.6f}, {:.6f}) probeK=({:.4f}, {:.4f}) probeOmega={:.6f}",
            resolution,
            activeSpectrumSampleCount,
            averageSpectralAmplitude,
            maxSpectralAmplitude,
            maxAngularFrequency,
            probeIndex,
            probeSample.h0[0],
            probeSample.h0[1],
            probeSample.h0Minus[0],
            probeSample.h0Minus[1],
            probeSample.waveVector[0],
            probeSample.waveVector[1],
            probeSample.angularFrequency);
    }

    void FFTOceanManager::UpdateSimulationConstants(float timeSeconds)
    {
        if (!mappedSimulationConstants_) {
            return;
        }

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
            ifftConstantsWriteIndex_ = kMaxIFFTPassCount - 1u;
        }

        const UINT slotSize = Align256(sizeof(IFFTConstants));
        IFFTConstants* constants = reinterpret_cast<IFFTConstants*>(
            mappedIFFTConstantsData_ + static_cast<size_t>(slotSize) * ifftConstantsWriteIndex_);
        constants->resolution = settings_.resolution;
        constants->stageIndex = stageIndex;
        constants->isHorizontal = isHorizontal ? 1u : 0u;
        constants->normalizationScale = normalizationScale;

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress =
            ifftConstantsBuffer_->GetGPUVirtualAddress() + static_cast<UINT64>(slotSize) * ifftConstantsWriteIndex_;
        ++ifftConstantsWriteIndex_;
        return gpuAddress;
    }

    void FFTOceanManager::DispatchEvolutionPass(ID3D12GraphicsCommandList* cmdList)
    {
        if (spectrumBufferDirty_) {
            spectrumBufferDirty_ = false;

            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: spectrum buffer updated on upload heap. bytes={}",
                spectrumUploadBuffer_->GetDesc().Width);
        }

        for (uint32_t i = 0; i < kPingPongCount; ++i) {
            ResourceBarrierHelper::Transition(
                cmdList,
                spectrumTextureA_[i].Get(),
                spectrumAState_[i],
                i == 0u ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON);
            ResourceBarrierHelper::Transition(
                cmdList,
                spectrumTextureB_[i].Get(),
                spectrumBState_[i],
                i == 0u ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COMMON);
        }

        cmdList->SetPipelineState(evolutionPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(evolutionPipeline_.GetComputeRootSignature());

        const int spectrumSlot = evolutionPipeline_.GetComputeRootParamIndex("gSpectrumSamples");
        if (spectrumSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumSlot), spectrumSrvHandle_);
        }

        const int spectrumAOutputSlot = evolutionPipeline_.GetComputeRootParamIndex("gHeightDisplacementXOutput");
        if (spectrumAOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumAOutputSlot), spectrumAUavHandle_[0]);
        }

        const int spectrumBOutputSlot = evolutionPipeline_.GetComputeRootParamIndex("gDisplacementZOutput");
        if (spectrumBOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumBOutputSlot), spectrumBUavHandle_[0]);
        }

        const int constantsSlot = evolutionPipeline_.GetComputeRootParamIndex("FFTOceanSimulationConstants");
        if (constantsSlot >= 0 && simulationConstantsBuffer_) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                simulationConstantsBuffer_->GetGPUVirtualAddress());
        }

        const UINT dispatchX = (settings_.resolution + 7u) / 8u;
        const UINT dispatchY = (settings_.resolution + 7u) / 8u;
        cmdList->Dispatch(dispatchX, dispatchY, 1u);

        ResourceBarrierHelper::UAV(cmdList, spectrumTextureA_[0].Get());
        ResourceBarrierHelper::UAV(cmdList, spectrumTextureB_[0].Get());
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
        ResourceBarrierHelper::Transition(cmdList, inputResource, inputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, outputResource, outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress =
            UpdateIFFTConstants(stageIndex, isHorizontal, normalizationScale);

        cmdList->SetPipelineState(pipeline.GetComputePSO());
        cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());

        const int inputSlot = pipeline.GetComputeRootParamIndex("gInputSpectrum");
        if (inputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(inputSlot), inputSrv);
        }

        const int outputSlot = pipeline.GetComputeRootParamIndex("gOutputSpectrum");
        if (outputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outputSlot), outputUav);
        }

        const int constantsSlot = pipeline.GetComputeRootParamIndex("FFTOceanIFFTConstants");
        if (constantsSlot >= 0 && ifftConstantsGpuAddress != 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                ifftConstantsGpuAddress);
        }

        const UINT dispatchX = (settings_.resolution + 7u) / 8u;
        const UINT dispatchY = (settings_.resolution + 7u) / 8u;
        cmdList->Dispatch(dispatchX, dispatchY, 1u);
        ResourceBarrierHelper::UAV(cmdList, outputResource);
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
        uint32_t writeIndex = (initialIndex + 1u) % kPingPongCount;
        const uint32_t log2Resolution = GetLog2Resolution();

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

        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = (stageIndex + 1u == log2Resolution)
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
        ResourceBarrierHelper::Transition(cmdList, spectrumAResource, spectrumAState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumBResource, spectrumBState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(finalizePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(finalizePipeline_.GetComputeRootSignature());

        const int spectrumASlot = finalizePipeline_.GetComputeRootParamIndex("gHeightDisplacementXSpectrum");
        if (spectrumASlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumASlot), spectrumASrv);
        }

        const int spectrumBSlot = finalizePipeline_.GetComputeRootParamIndex("gDisplacementZSpectrum");
        if (spectrumBSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(spectrumBSlot), spectrumBSrv);
        }

        const int displacementOutputSlot = finalizePipeline_.GetComputeRootParamIndex("gDisplacementOutput");
        if (displacementOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(displacementOutputSlot), displacementUavHandle_);
        }

        const int normalOutputSlot = finalizePipeline_.GetComputeRootParamIndex("gNormalOutput");
        if (normalOutputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(normalOutputSlot), normalUavHandle_);
        }

        const int constantsSlot = finalizePipeline_.GetComputeRootParamIndex("FFTOceanSimulationConstants");
        if (constantsSlot >= 0 && simulationConstantsBuffer_) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot),
                simulationConstantsBuffer_->GetGPUVirtualAddress());
        }

        const UINT dispatchX = (settings_.resolution + 7u) / 8u;
        const UINT dispatchY = (settings_.resolution + 7u) / 8u;
        cmdList->Dispatch(dispatchX, dispatchY, 1u);
    }

    void FFTOceanManager::LogPendingDebugReadback()
    {
        if (!debugReadbackPending_ || !displacementReadbackBuffer_ || !normalReadbackBuffer_) {
            return;
        }

        void* displacementMapped = nullptr;
        void* normalMapped = nullptr;
        const D3D12_RANGE displacementReadRange{ 0, static_cast<SIZE_T>(displacementReadbackBytes_) };
        const D3D12_RANGE normalReadRange{ 0, static_cast<SIZE_T>(normalReadbackBytes_) };
        const HRESULT displacementHr = displacementReadbackBuffer_->Map(0, &displacementReadRange, &displacementMapped);
        const HRESULT normalHr = normalReadbackBuffer_->Map(0, &normalReadRange, &normalMapped);
        if (FAILED(displacementHr) || FAILED(normalHr) || !displacementMapped || !normalMapped) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: debug readback map failed. displacementHr={:#x} normalHr={:#x}",
                static_cast<uint32_t>(displacementHr),
                static_cast<uint32_t>(normalHr));
            return;
        }

        const uint32_t centerX = settings_.resolution / 2u;
        const uint32_t centerY = settings_.resolution / 2u;
        const Float4DebugValue centerDisplacement = LoadHalf4(
            static_cast<const uint8_t*>(displacementMapped),
            centerX,
            centerY,
            displacementReadbackLayout_.Footprint.RowPitch);
        const Float4DebugValue centerNormal = LoadHalf4(
            static_cast<const uint8_t*>(normalMapped),
            centerX,
            centerY,
            normalReadbackLayout_.Footprint.RowPitch);

        float minHeight = (std::numeric_limits<float>::max)();
        float maxHeight = (std::numeric_limits<float>::lowest)();
        float accumulatedAbsHeight = 0.0f;
        float maxHorizontalMagnitude = 0.0f;
        const uint8_t* displacementBase = static_cast<const uint8_t*>(displacementMapped);
        for (uint32_t y = 0; y < settings_.resolution; ++y) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const Float4DebugValue displacementValue = LoadHalf4(
                    displacementBase,
                    x,
                    y,
                    displacementReadbackLayout_.Footprint.RowPitch);
                minHeight = (std::min)(minHeight, displacementValue.y);
                maxHeight = (std::max)(maxHeight, displacementValue.y);
                accumulatedAbsHeight += std::abs(displacementValue.y);
                const float horizontalMagnitude = std::sqrt(
                    displacementValue.x * displacementValue.x + displacementValue.z * displacementValue.z);
                maxHorizontalMagnitude = (std::max)(maxHorizontalMagnitude, horizontalMagnitude);
            }
        }

        displacementReadbackBuffer_->Unmap(0, nullptr);
        normalReadbackBuffer_->Unmap(0, nullptr);
        debugReadbackPending_ = false;

        const float averageAbsHeight = accumulatedAbsHeight / static_cast<float>(settings_.resolution * settings_.resolution);
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: readback seq={} centerDisp=({:.6f}, {:.6f}, {:.6f}, {:.6f}) centerNormal=({:.6f}, {:.6f}, {:.6f}, {:.6f}) height[min,max,avgAbs]=[{:.6f}, {:.6f}, {:.6f}] maxHorizontal={:.6f}",
            debugReadbackSequence_,
            centerDisplacement.x,
            centerDisplacement.y,
            centerDisplacement.z,
            centerDisplacement.w,
            centerNormal.x,
            centerNormal.y,
            centerNormal.z,
            centerNormal.w,
            minHeight,
            maxHeight,
            averageAbsHeight,
            maxHorizontalMagnitude);
    }

    void FFTOceanManager::LogPendingEvolutionDebugReadback()
    {
        if (!evolutionDebugReadbackPending_ || !evolutionAReadbackBuffer_ || !evolutionBReadbackBuffer_) {
            return;
        }

        void* evolutionAMapped = nullptr;
        void* evolutionBMapped = nullptr;
        const D3D12_RANGE evolutionAReadRange{ 0, static_cast<SIZE_T>(evolutionAReadbackBytes_) };
        const D3D12_RANGE evolutionBReadRange{ 0, static_cast<SIZE_T>(evolutionBReadbackBytes_) };
        const HRESULT evolutionAHr = evolutionAReadbackBuffer_->Map(0, &evolutionAReadRange, &evolutionAMapped);
        const HRESULT evolutionBHr = evolutionBReadbackBuffer_->Map(0, &evolutionBReadRange, &evolutionBMapped);
        if (FAILED(evolutionAHr) || FAILED(evolutionBHr) || !evolutionAMapped || !evolutionBMapped) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: evolution readback map failed. aHr={:#x} bHr={:#x}",
                static_cast<uint32_t>(evolutionAHr),
                static_cast<uint32_t>(evolutionBHr));
            return;
        }

        const uint32_t centerX = settings_.resolution / 2u;
        const uint32_t centerY = settings_.resolution / 2u;
        const Float4DebugValue centerA = LoadFloat4(
            static_cast<const uint8_t*>(evolutionAMapped),
            centerX,
            centerY,
            evolutionAReadbackLayout_.Footprint.RowPitch);
        const Float4DebugValue centerB = LoadFloat4(
            static_cast<const uint8_t*>(evolutionBMapped),
            centerX,
            centerY,
            evolutionBReadbackLayout_.Footprint.RowPitch);

        float maxAbsA = 0.0f;
        float maxAbsB = 0.0f;
        const uint8_t* evolutionABase = static_cast<const uint8_t*>(evolutionAMapped);
        const uint8_t* evolutionBBase = static_cast<const uint8_t*>(evolutionBMapped);
        for (uint32_t y = 0; y < settings_.resolution; ++y) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const Float4DebugValue valueA = LoadFloat4(evolutionABase, x, y, evolutionAReadbackLayout_.Footprint.RowPitch);
                const Float4DebugValue valueB = LoadFloat4(evolutionBBase, x, y, evolutionBReadbackLayout_.Footprint.RowPitch);
                maxAbsA = (std::max)(maxAbsA, (std::max)((std::max)(std::abs(valueA.x), std::abs(valueA.y)), (std::max)(std::abs(valueA.z), std::abs(valueA.w))));
                maxAbsB = (std::max)(maxAbsB, (std::max)((std::max)(std::abs(valueB.x), std::abs(valueB.y)), (std::max)(std::abs(valueB.z), std::abs(valueB.w))));
            }
        }

        evolutionAReadbackBuffer_->Unmap(0, nullptr);
        evolutionBReadbackBuffer_->Unmap(0, nullptr);
        evolutionDebugReadbackPending_ = false;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: evolutionReadback seq={} centerA=({:.6f}, {:.6f}, {:.6f}, {:.6f}) centerB=({:.6f}, {:.6f}, {:.6f}, {:.6f}) maxAbsA={:.6f} maxAbsB={:.6f}",
            evolutionDebugReadbackSequence_,
            centerA.x,
            centerA.y,
            centerA.z,
            centerA.w,
            centerB.x,
            centerB.y,
            centerB.z,
            centerB.w,
            maxAbsA,
            maxAbsB);
    }

    void FFTOceanManager::LogPendingIFFTDebugReadback()
    {
        if (!ifftDebugReadbackPending_ || !ifftAReadbackBuffer_ || !ifftBReadbackBuffer_) {
            return;
        }

        void* ifftAMapped = nullptr;
        void* ifftBMapped = nullptr;
        const D3D12_RANGE ifftAReadRange{ 0, static_cast<SIZE_T>(ifftAReadbackBytes_) };
        const D3D12_RANGE ifftBReadRange{ 0, static_cast<SIZE_T>(ifftBReadbackBytes_) };
        const HRESULT ifftAHr = ifftAReadbackBuffer_->Map(0, &ifftAReadRange, &ifftAMapped);
        const HRESULT ifftBHr = ifftBReadbackBuffer_->Map(0, &ifftBReadRange, &ifftBMapped);
        if (FAILED(ifftAHr) || FAILED(ifftBHr) || !ifftAMapped || !ifftBMapped) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: ifft readback map failed. aHr={:#x} bHr={:#x}",
                static_cast<uint32_t>(ifftAHr),
                static_cast<uint32_t>(ifftBHr));
            return;
        }

        const uint32_t centerX = settings_.resolution / 2u;
        const uint32_t centerY = settings_.resolution / 2u;
        const Float4DebugValue centerA = LoadFloat4(
            static_cast<const uint8_t*>(ifftAMapped),
            centerX,
            centerY,
            ifftAReadbackLayout_.Footprint.RowPitch);
        const Float4DebugValue centerB = LoadFloat4(
            static_cast<const uint8_t*>(ifftBMapped),
            centerX,
            centerY,
            ifftBReadbackLayout_.Footprint.RowPitch);

        float maxAbsAX = 0.0f;
        float maxAbsAY = 0.0f;
        float maxAbsAZ = 0.0f;
        float maxAbsAW = 0.0f;
        float maxAbsBX = 0.0f;
        float maxAbsBY = 0.0f;
        const uint8_t* ifftABase = static_cast<const uint8_t*>(ifftAMapped);
        const uint8_t* ifftBBase = static_cast<const uint8_t*>(ifftBMapped);
        for (uint32_t y = 0; y < settings_.resolution; ++y) {
            for (uint32_t x = 0; x < settings_.resolution; ++x) {
                const Float4DebugValue valueA = LoadFloat4(ifftABase, x, y, ifftAReadbackLayout_.Footprint.RowPitch);
                const Float4DebugValue valueB = LoadFloat4(ifftBBase, x, y, ifftBReadbackLayout_.Footprint.RowPitch);
                maxAbsAX = (std::max)(maxAbsAX, std::abs(valueA.x));
                maxAbsAY = (std::max)(maxAbsAY, std::abs(valueA.y));
                maxAbsAZ = (std::max)(maxAbsAZ, std::abs(valueA.z));
                maxAbsAW = (std::max)(maxAbsAW, std::abs(valueA.w));
                maxAbsBX = (std::max)(maxAbsBX, std::abs(valueB.x));
                maxAbsBY = (std::max)(maxAbsBY, std::abs(valueB.y));
            }
        }

        ifftAReadbackBuffer_->Unmap(0, nullptr);
        ifftBReadbackBuffer_->Unmap(0, nullptr);
        ifftDebugReadbackPending_ = false;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: ifftReadback seq={} centerA=({:.6f}, {:.6f}, {:.6f}, {:.6f}) centerB=({:.6f}, {:.6f}, {:.6f}, {:.6f}) maxAbsA[x,y,z,w]=[{:.6f}, {:.6f}, {:.6f}, {:.6f}] maxAbsB[x,y]=[{:.6f}, {:.6f}]",
            ifftDebugReadbackSequence_,
            centerA.x,
            centerA.y,
            centerA.z,
            centerA.w,
            centerB.x,
            centerB.y,
            centerB.z,
            centerB.w,
            maxAbsAX,
            maxAbsAY,
            maxAbsAZ,
            maxAbsAW,
            maxAbsBX,
            maxAbsBY);
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
        uint32_t log2Resolution = 0u;
        uint32_t resolution = settings_.resolution;
        while (resolution > 1u) {
            resolution >>= 1u;
            ++log2Resolution;
        }
        return log2Resolution;
    }
}
