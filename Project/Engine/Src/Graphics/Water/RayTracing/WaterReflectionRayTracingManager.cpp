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
        };
    }

    static_assert(sizeof(WaterReflectionConstants) == 192,
        "WaterReflectionConstants size mismatch with HLSL cbuffer");

    bool WaterReflectionRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(dxCommon, descriptorManager, asMgr,
            "WaterReflectionRayTracingManager", "RTWaterReflection")) {
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

    void WaterReflectionRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData =
            ResolveSurfaceDataForDispatch(surfaceData, resolvedSurfaceData);

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, width, height, dispatchSurfaceData, sceneDepthSRV, sceneColorSRV);

        DispatchResources resources;
        if (!BeginDispatch(cmdList, width, height, viewIndex, resources)) {
            return;
        }

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

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;

        UploadSurfaceDataForDispatch(dispatchSurfaceData);

        resources.cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        resources.cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, resources.outputResource, *resources.outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gReflectionOutput")),
            resources.outputUavHandle);
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
        resources.cmdList4->DispatchRays(&dispatchDesc);
        lastDispatchInfo_.status = RayTracingDispatchStatus::Dispatched;

        EndOutputWrite(
            cmdList,
            resources.outputResource,
            *resources.outputCurrentState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
