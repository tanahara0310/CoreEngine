#include "pch.h"
#include "WaterReflectionRayTracingManager.h"

#include <algorithm>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RayTracing/RayTracingPipelineBuilder.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct WaterReflectionConstants {
            Matrix4x4 viewProjection;
            Matrix4x4 invViewProjection;
            float cameraPosition[3];
            float waterHeight;
            float surfaceBias;
            float maxRayDistance;
            float unused0;
            float unused1;
            float screenWidth;
            float screenHeight;
            float maxReflectionOffsetPixels;
            uint32_t fftOceanEnabled;
            float fftOceanPatchLength;
            uint32_t fftOceanResolution;
            float debugDisplayScale;
            uint32_t debugViewMode;
            float fftOceanUVScale[2];
            float fftOceanUVOffset[2];
        };

        const char* ToString(WaterReflectionRayTracingManager::DispatchStatus status)
        {
            switch (status) {
            case WaterReflectionRayTracingManager::DispatchStatus::None: return "None";
            case WaterReflectionRayTracingManager::DispatchStatus::NotInitialized: return "NotInitialized";
            case WaterReflectionRayTracingManager::DispatchStatus::RayTracingUnsupported: return "RayTracingUnsupported";
            case WaterReflectionRayTracingManager::DispatchStatus::NoBLAS: return "NoBLAS";
            case WaterReflectionRayTracingManager::DispatchStatus::OutputAllocationFailed: return "OutputAllocationFailed";
            case WaterReflectionRayTracingManager::DispatchStatus::CommandList4Unavailable: return "CommandList4Unavailable";
            case WaterReflectionRayTracingManager::DispatchStatus::Dispatched: return "Dispatched";
            default: return "Unknown";
            }
        }
    }

    static_assert(sizeof(WaterReflectionConstants) == 208,
        "WaterReflectionConstants size mismatch with HLSL cbuffer");

    bool WaterReflectionRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(dxCommon, descriptorManager, asMgr, "WaterReflectionRayTracingManager")) {
            log.Log("WaterReflectionRayTracingManager: DXR not supported, skipping",
                LogLevel::Warn,
                LogCategory::Graphics);
            return false;
        }

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        shaderBlob_.Attach(shaderCompiler.CompileShaderLibrary(L"Engine/Assets/Shaders/Water/RayTracing/RTWaterReflection.hlsl"));
        if (!shaderBlob_) {
            log.Log("WaterReflectionRayTracingManager: Shader compile failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        globalRootSigMgr_
            .AddUAVTable("gReflectionOutput", 0)
            .AddSRVTable("gScene", 0)
            .AddSRVTable("gSceneDepth", 1)
            .AddSRVTable("gSceneColor", 2)
            .AddSRVTable("gFFTOceanDisplacement", 3)
            .AddSRVTable("gFFTOceanNormal", 4)
            .AddCBV("gWaterSurfaceData", 1)
            .AddRootConstants("WaterReflectionConstants", 0,
                sizeof(WaterReflectionConstants) / sizeof(uint32_t));
        if (!globalRootSigMgr_.Build(dxCommon_->GetDevice())) {
            log.Log("WaterReflectionRayTracingManager: Global root signature build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob_.Get())
            .AddHitGroup({ L"RTWaterReflectionHitGroup", L"RTWaterReflectionClosestHit" })
            .SetShaderConfig(sizeof(float) * 2)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        if (!pipelineBuilder.Build(dxCommon_->GetDevice(), stateObject_, stateObjectProperties_)) {
            log.Log("WaterReflectionRayTracingManager: State object build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        shaderTableBuilder_
            .SetRayGenShader(L"RTWaterReflectionRayGen")
            .AddMissShader(L"RTWaterReflectionMiss")
            .AddHitGroup(L"RTWaterReflectionHitGroup");
        if (!shaderTableBuilder_.Build(dxCommon_->GetDevice(), stateObjectProperties_.Get())) {
            log.Log("WaterReflectionRayTracingManager: Shader table build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        isInitialized_ = true;
        shaderBlob_.Reset();

        log.Log("WaterReflectionRayTracingManager: Initialized successfully",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    bool WaterReflectionRayTracingManager::EnsureConstantBuffer()
    {
        return EnsureSurfaceConstantBuffer(GetSurfaceConstantBufferSize(), "WaterReflectionRayTracingManager");
    }

    bool WaterReflectionRayTracingManager::EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex)
    {
        return EnsureOutputTextureBase(
            width,
            height,
            viewIndex,
            "WaterReflectionRayTracingManager",
            "RTWaterReflection_UAV_v" + std::to_string(viewIndex),
            "RTWaterReflection_SRV_v" + std::to_string(viewIndex));
    }

    void WaterReflectionRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        ReleaseOutputIfSizeMismatchBase(width, height, static_cast<uint32_t>(viewId));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterReflectionRayTracingManager::GetReflectionSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    ID3D12Resource* WaterReflectionRayTracingManager::GetReflectionResource(ViewID viewId) const
    {
        return GetOutputResourceBase(static_cast<uint32_t>(viewId));
    }

    D3D12_RESOURCE_STATES& WaterReflectionRayTracingManager::GetReflectionCurrentState(ViewID viewId)
    {
        return GetOutputCurrentStateBase(static_cast<uint32_t>(viewId));
    }

    void WaterReflectionRayTracingManager::SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        SetSurfaceModelProviderBase(provider);
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterReflectionRayTracingManager::GetSurfaceModelProvider() const
    {
        return GetSurfaceModelProviderBase();
    }

    void WaterReflectionRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const WaterSurfaceData& surfaceData,
        const FFTOceanReflectionInput& fftOceanInput,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData = ResolveSurfaceDataForDispatch(
            surfaceData,
            resolvedSurfaceData,
            "WaterReflectionRayTracingManager");

        lastDiagnostics_ = {};
        lastDiagnostics_.viewId = viewId;
        lastDiagnostics_.waterHeight = dispatchSurfaceData.waterHeight;
        lastDiagnostics_.width = width;
        lastDiagnostics_.height = height;
        lastDiagnostics_.sceneDepthSrv = sceneDepthSRV.ptr;
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
            "WaterReflectionRayTracingManager",
            "RTWaterReflection_UAV_v" + std::to_string(viewIndex),
            "RTWaterReflection_SRV_v" + std::to_string(viewIndex),
            guardStatus,
            outputSrvHandle,
            outputUavHandle,
            outputResource,
            outputCurrentState,
            cmdList4)) {
            switch (guardStatus) {
            case DispatchGuardStatus::NotInitialized:
                lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::NotInitialized;
                break;
            case DispatchGuardStatus::RayTracingUnsupported:
                lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::RayTracingUnsupported;
                break;
            case DispatchGuardStatus::InvalidCommandList:
            case DispatchGuardStatus::CommandList4Unavailable:
                lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::CommandList4Unavailable;
                break;
            case DispatchGuardStatus::NoBLAS:
                lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::NoBLAS;
                break;
            case DispatchGuardStatus::OutputAllocationFailed:
                lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::OutputAllocationFailed;
                break;
            default:
                lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::RayTracingUnsupported;
                break;
            }
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterReflectionRayTracingManager: dispatch skipped. status={} initialized={} cmdList={} asMgr={} supported={}",
                ToString(lastDiagnostics_.status),
                isInitialized_,
                cmdList != nullptr,
                asMgr_ != nullptr,
                asMgr_ ? asMgr_->IsSupported() : false);
            return;
        }
        lastDiagnostics_.outputSrv = outputSrvHandle.ptr;

        WaterReflectionConstants constants{};
        constants.viewProjection = viewProjection;
        constants.invViewProjection = MathCore::Matrix::Inverse(viewProjection);
        constants.cameraPosition[0] = cameraPosition.x;
        constants.cameraPosition[1] = cameraPosition.y;
        constants.cameraPosition[2] = cameraPosition.z;
        constants.waterHeight = dispatchSurfaceData.waterHeight;
        constants.surfaceBias = settings_.surfaceBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        constants.unused0 = 0.0f;
        constants.unused1 = 0.0f;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.maxReflectionOffsetPixels = settings_.maxReflectionOffsetPixels;
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

        const WaterSurfaceConstants surfaceConstants = UploadSurfaceDataForDispatch(
            dispatchSurfaceData,
            "WaterReflectionRayTracingManager");
        (void)surfaceConstants;

        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, outputResource, *outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gReflectionOutput")),
            outputUavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gSceneDepth")),
            sceneDepthSRV);
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
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("WaterReflectionConstants")),
            sizeof(WaterReflectionConstants) / sizeof(uint32_t),
            &constants,
            0);

        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        cmdList4->DispatchRays(&dispatchDesc);
        lastDiagnostics_.status = WaterReflectionRayTracingManager::DispatchStatus::Dispatched;

        EndOutputWrite(
            cmdList,
            outputResource,
            *outputCurrentState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
