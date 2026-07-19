#include "pch.h"
#include "WaterRefractionRayTracingManager.h"

#include <algorithm>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RayTracing/RayTracingPipelineBuilder.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct WaterRefractionConstants {
            Matrix4x4 viewProjection;
            float cameraPosition[3];
            float waterHeight;
            float surfaceBias;
            float maxRayDistance;
            float refractionEta;
            float absorptionCoeff;
            float screenWidth;
            float screenHeight;
            float maxRefractionOffsetPixels;
            uint32_t fftOceanEnabled;
            float fftOceanPatchLength;
            uint32_t fftOceanResolution;
            float debugDisplayScale;
            uint32_t debugViewMode;
            // ワールドXZ → FFT テクスチャ UV の写像（uv = worldXZ * scale + offset）。
            // ラスタ描画（FFTWater.VS）と同じ波面を RT が評価するために必須
            float fftOceanUVScale[2];
            float fftOceanUVOffset[2];
        };

        const char* ToString(WaterRefractionRayTracingManager::DispatchStatus status)
        {
            switch (status) {
            case WaterRefractionRayTracingManager::DispatchStatus::None: return "None";
            case WaterRefractionRayTracingManager::DispatchStatus::NotInitialized: return "NotInitialized";
            case WaterRefractionRayTracingManager::DispatchStatus::RayTracingUnsupported: return "RayTracingUnsupported";
            case WaterRefractionRayTracingManager::DispatchStatus::NoBLAS: return "NoBLAS";
            case WaterRefractionRayTracingManager::DispatchStatus::OutputAllocationFailed: return "OutputAllocationFailed";
            case WaterRefractionRayTracingManager::DispatchStatus::CommandList4Unavailable: return "CommandList4Unavailable";
            case WaterRefractionRayTracingManager::DispatchStatus::Dispatched: return "Dispatched";
            default: return "Unknown";
            }
        }
    }

    static_assert(sizeof(WaterRefractionConstants) == 144,
        "WaterRefractionConstants size mismatch with HLSL cbuffer");
    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");

    bool WaterRefractionRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(dxCommon, descriptorManager, asMgr, "WaterRefractionRayTracingManager")) {
            log.Log("WaterRefractionRayTracingManager: DXR not supported, skipping",
                LogLevel::Warn,
                LogCategory::Graphics);
            return false;
        }

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        shaderBlob_.Attach(shaderCompiler.CompileShaderLibrary(L"Engine/Assets/Shaders/Water/RayTracing/RTWaterRefraction.hlsl"));
        if (!shaderBlob_) {
            log.Log("WaterRefractionRayTracingManager: Shader compile failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        globalRootSigMgr_
            .AddUAVTable("gRefractionOutput", 0)
            .AddSRVTable("gScene", 0)
            .AddSRVTable("gWorldPosition", 1)
            .AddSRVTable("gSceneColor", 2)
            .AddSRVTable("gFFTOceanDisplacement", 3)
            .AddSRVTable("gFFTOceanNormal", 4)
            .AddCBV("gWaterSurfaceData", 1)
            .AddRootConstants("WaterRefractionConstants", 0,
                sizeof(WaterRefractionConstants) / sizeof(uint32_t));
        if (!globalRootSigMgr_.Build(dxCommon_->GetDevice())) {
            log.Log("WaterRefractionRayTracingManager: Global root signature build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob_.Get())
            .AddHitGroup({ L"RTWaterRefractionHitGroup", L"RTWaterRefractionClosestHit" })
            .SetShaderConfig(sizeof(float) * 2)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        if (!pipelineBuilder.Build(dxCommon_->GetDevice(), stateObject_, stateObjectProperties_)) {
            log.Log("WaterRefractionRayTracingManager: State object build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        shaderTableBuilder_
            .SetRayGenShader(L"RTWaterRefractionRayGen")
            .AddMissShader(L"RTWaterRefractionMiss")
            .AddHitGroup(L"RTWaterRefractionHitGroup");
        if (!shaderTableBuilder_.Build(dxCommon_->GetDevice(), stateObjectProperties_.Get())) {
            log.Log("WaterRefractionRayTracingManager: Shader table build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        isInitialized_ = true;
        shaderBlob_.Reset();

        log.Log("WaterRefractionRayTracingManager: Initialized successfully",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    bool WaterRefractionRayTracingManager::EnsureConstantBuffer()
    {
        return EnsureSurfaceConstantBuffer(GetSurfaceConstantBufferSize(), "WaterRefractionRayTracingManager");
    }

    bool WaterRefractionRayTracingManager::EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex)
    {
        return EnsureOutputTextureBase(
            width,
            height,
            viewIndex,
            "WaterRefractionRayTracingManager",
            "RTWaterRefraction_UAV_v" + std::to_string(viewIndex),
            "RTWaterRefraction_SRV_v" + std::to_string(viewIndex));
    }

    void WaterRefractionRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        ReleaseOutputIfSizeMismatchBase(width, height, static_cast<uint32_t>(viewId));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterRefractionRayTracingManager::GetRefractionSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    ID3D12Resource* WaterRefractionRayTracingManager::GetRefractionResource(ViewID viewId) const
    {
        return GetOutputResourceBase(static_cast<uint32_t>(viewId));
    }

    D3D12_RESOURCE_STATES& WaterRefractionRayTracingManager::GetRefractionCurrentState(ViewID viewId)
    {
        return GetOutputCurrentStateBase(static_cast<uint32_t>(viewId));
    }

    void WaterRefractionRayTracingManager::SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        SetSurfaceModelProviderBase(provider);
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterRefractionRayTracingManager::GetSurfaceModelProvider() const
    {
        return GetSurfaceModelProviderBase();
    }

    void WaterRefractionRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const WaterSurfaceData& surfaceData,
        const FFTOceanRefractionInput& fftOceanInput,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData = ResolveSurfaceDataForDispatch(
            surfaceData,
            resolvedSurfaceData,
            "WaterRefractionRayTracingManager");

        lastDiagnostics_ = {};
        lastDiagnostics_.viewId = viewId;
        lastDiagnostics_.waterHeight = dispatchSurfaceData.waterHeight;
        lastDiagnostics_.width = width;
        lastDiagnostics_.height = height;
        lastDiagnostics_.worldPositionSrv = worldPositionSRV.ptr;
        lastDiagnostics_.sceneColorSrv = sceneColorSRV.ptr;
        lastDiagnostics_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;

        DispatchGuardStatus guardStatus = DispatchGuardStatus::Ok;
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        D3D12_GPU_DESCRIPTOR_HANDLE outputSrvHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle{};
        ID3D12Resource* outputResource = nullptr;
        D3D12_RESOURCE_STATES* outputCurrentState = nullptr;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (!PrepareDispatchResources(
            cmdList,
            width,
            height,
            viewIndex,
            GetSurfaceConstantBufferSize(),
            "WaterRefractionRayTracingManager",
            "RTWaterRefraction_UAV_v" + std::to_string(viewIndex),
            "RTWaterRefraction_SRV_v" + std::to_string(viewIndex),
            guardStatus,
            outputSrvHandle,
            outputUavHandle,
            outputResource,
            outputCurrentState,
            cmdList4)) {
            switch (guardStatus) {
            case DispatchGuardStatus::NotInitialized:
                lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::NotInitialized;
                break;
            case DispatchGuardStatus::RayTracingUnsupported:
                lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::RayTracingUnsupported;
                break;
            case DispatchGuardStatus::InvalidCommandList:
            case DispatchGuardStatus::CommandList4Unavailable:
                lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::CommandList4Unavailable;
                break;
            case DispatchGuardStatus::NoBLAS:
                lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::NoBLAS;
                break;
            case DispatchGuardStatus::OutputAllocationFailed:
                lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::OutputAllocationFailed;
                break;
            default:
                lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::RayTracingUnsupported;
                break;
            }
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: dispatch skipped. status={} initialized={} cmdList={} asMgr={} supported={}",
                ToString(lastDiagnostics_.status),
                isInitialized_,
                cmdList != nullptr,
                asMgr_ != nullptr,
                asMgr_ ? asMgr_->IsSupported() : false);
            return;
        }
        lastDiagnostics_.outputSrv = outputSrvHandle.ptr;

        WaterRefractionConstants constants{};
        constants.viewProjection = viewProjection;
        constants.cameraPosition[0] = cameraPosition.x;
        constants.cameraPosition[1] = cameraPosition.y;
        constants.cameraPosition[2] = cameraPosition.z;
        constants.waterHeight = dispatchSurfaceData.waterHeight;
        constants.surfaceBias = settings_.surfaceBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        constants.refractionEta = 1.0f / (std::max)(settings_.waterRefractiveIndex, 1.0e-4f);
        constants.absorptionCoeff = settings_.absorptionCoeff;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.maxRefractionOffsetPixels = settings_.maxRefractionOffsetPixels;
        constants.fftOceanEnabled = fftOceanInput.enabled;
        constants.fftOceanPatchLength = fftOceanInput.patchLength;
        constants.fftOceanResolution = fftOceanInput.resolution;
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;
        constants.fftOceanUVScale[0] = fftOceanInput.uvScale[0];
        constants.fftOceanUVScale[1] = fftOceanInput.uvScale[1];
        constants.fftOceanUVOffset[0] = fftOceanInput.uvOffset[0];
        constants.fftOceanUVOffset[1] = fftOceanInput.uvOffset[1];

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterRefractionRayTracingManager: dispatch begin. viewId={} waterHeight={:.3f} width={} height={} blasCount={} worldPosSRV=0x{:X} sceneColorSRV=0x{:X} outputSRV=0x{:X} eta={:.4f} maxRayDistance={:.3f} absorptionCoeff={:.3f} surfaceBias={:.4f} maxOffsetPx={:.3f} cameraPos=({:.3f}, {:.3f}, {:.3f}) simulationType={} activeWaveCount={} waveTime={:.3f} fftEnabled={} fftResolution={} fftPatchLength={:.3f} fftDispSRV=0x{:X} fftNormalSRV=0x{:X} debugViewMode={} debugScale={:.3f} debugLogEnabled={}",
            viewIndex,
            dispatchSurfaceData.waterHeight,
            width,
            height,
            lastDiagnostics_.blasCount,
            worldPositionSRV.ptr,
            sceneColorSRV.ptr,
            outputSrvHandle.ptr,
            constants.refractionEta,
            constants.maxRayDistance,
            constants.absorptionCoeff,
            constants.surfaceBias,
            constants.maxRefractionOffsetPixels,
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            dispatchSurfaceData.simulationType,
            dispatchSurfaceData.activeWaveCount,
            dispatchSurfaceData.time,
            constants.fftOceanEnabled,
            constants.fftOceanResolution,
            constants.fftOceanPatchLength,
            fftDisplacementSRV.ptr,
            fftNormalSRV.ptr,
            constants.debugViewMode,
            constants.debugDisplayScale,
            settings_.debugLogEnabled);

        if (settings_.debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: RT refraction debug active. mode={} scale={:.3f} maxOffsetPx={:.3f} absorptionCoeff={:.3f}",
                settings_.debugViewMode,
                settings_.debugDisplayScale,
                settings_.maxRefractionOffsetPixels,
                settings_.absorptionCoeff);
        }

        const WaterSurfaceConstants surfaceConstants = UploadSurfaceDataForDispatch(
            dispatchSurfaceData,
            "WaterRefractionRayTracingManager");

        if (surfaceConstants.activeWaveCount > 0) {
            const WaterWaveParam& firstWave = surfaceConstants.waves[0];
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: surface constants uploaded. waterHeight={:.3f} simulationType={} activeWaveCount={} waveTime={:.3f} firstWave(dir=({:.3f}, {:.3f}) amp={:.4f} len={:.3f} speed={:.3f} steep={:.3f} phase={:.3f})",
                surfaceConstants.waterHeight,
                surfaceConstants.simulationType,
                surfaceConstants.activeWaveCount,
                surfaceConstants.time,
                firstWave.direction[0],
                firstWave.direction[1],
                firstWave.amplitude,
                firstWave.wavelength,
                firstWave.speed,
                firstWave.steepness,
                firstWave.phaseOffset);
        } else {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: surface constants uploaded. waterHeight={:.3f} simulationType={} activeWaveCount=0 waveTime={:.3f}",
                surfaceConstants.waterHeight,
                surfaceConstants.simulationType,
                surfaceConstants.time);
        }

        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, outputResource, *outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gRefractionOutput")),
            outputUavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWorldPosition")),
            worldPositionSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gSceneColor")),
            sceneColorSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gFFTOceanDisplacement")),
            fftDisplacementSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gFFTOceanNormal")),
            fftNormalSRV);
        cmdList->SetComputeRootConstantBufferView(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWaterSurfaceData")),
            constantBuffer_->GetGPUVirtualAddress());
        cmdList->SetComputeRoot32BitConstants(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("WaterRefractionConstants")),
            sizeof(WaterRefractionConstants) / sizeof(uint32_t),
            &constants,
            0);

        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        cmdList4->DispatchRays(&dispatchDesc);
        lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::Dispatched;

        EndOutputWrite(
            cmdList,
            outputResource,
            *outputCurrentState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterRefractionRayTracingManager: dispatch end. status={} viewId={} outputState={}",
            ToString(lastDiagnostics_.status),
            viewIndex,
            static_cast<uint32_t>(*outputCurrentState));
    }
}
