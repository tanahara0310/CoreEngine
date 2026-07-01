#include "pch.h"
#include "WaterCausticsRayTracingManager.h"

#include "AccelerationStructureManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "RayTracingPipelineBuilder.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct alignas(16) WaterCausticsConstants {
            float maxTraceDistance;
            float surfaceBias;
            float intensityScale;
            float waterHeight;
            float lightDirection[3];
            float screenWidth;
            float screenHeight;
            float padding[3];
        };

        const char* ToString(WaterCausticsRayTracingManager::DispatchStatus status)
        {
            switch (status) {
            case WaterCausticsRayTracingManager::DispatchStatus::None: return "None";
            case WaterCausticsRayTracingManager::DispatchStatus::NotInitialized: return "NotInitialized";
            case WaterCausticsRayTracingManager::DispatchStatus::RayTracingUnsupported: return "RayTracingUnsupported";
            case WaterCausticsRayTracingManager::DispatchStatus::NoBLAS: return "NoBLAS";
            case WaterCausticsRayTracingManager::DispatchStatus::OutputAllocationFailed: return "OutputAllocationFailed";
            case WaterCausticsRayTracingManager::DispatchStatus::CommandList4Unavailable: return "CommandList4Unavailable";
            case WaterCausticsRayTracingManager::DispatchStatus::Dispatched: return "Dispatched";
            default: return "Unknown";
            }
        }
    }

    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");
    static_assert(sizeof(WaterCausticsConstants) == 48,
        "WaterCausticsConstants size mismatch with HLSL cbuffer");

    bool WaterCausticsRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        if (!InitializeBase(dxCommon, descriptorManager, asMgr, "WaterCausticsRayTracingManager")) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: DXR unsupported. initialization skipped.");
            return false;
        }

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        shaderBlob_.Attach(shaderCompiler.CompileShaderLibrary(L"Application/Assets/Shaders/Water/RTWaterCaustics.hlsl"));
        if (!shaderBlob_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: shader compile failed.");
            return false;
        }

        globalRootSigMgr_
            .AddUAVTable("gCausticsOutput", 0)
            .AddSRVTable("gScene", 0)
            .AddSRVTable("gWorldPosition", 1)
            .AddSRVTable("gNormalRoughness", 2)
            .AddCBV("gWaterSurfaceData", 1)
            .AddRootConstants("WaterCausticsConstants", 0,
                sizeof(WaterCausticsConstants) / sizeof(uint32_t));
        if (!globalRootSigMgr_.Build(dxCommon_->GetDevice())) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: global root signature build failed.");
            return false;
        }

        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob_.Get())
            .AddHitGroup({ L"RTWaterCausticsHitGroup", L"RTWaterCausticsClosestHit" })
            .SetShaderConfig(sizeof(float) * 4)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        if (!pipelineBuilder.Build(dxCommon_->GetDevice(), stateObject_, stateObjectProperties_)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: state object build failed.");
            return false;
        }

        shaderTableBuilder_
            .SetRayGenShader(L"RTWaterCausticsRayGen")
            .AddMissShader(L"RTWaterCausticsMiss")
            .AddHitGroup(L"RTWaterCausticsHitGroup");
        if (!shaderTableBuilder_.Build(dxCommon_->GetDevice(), stateObjectProperties_.Get())) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: shader table build failed.");
            return false;
        }

        isInitialized_ = true;
        shaderBlob_.Reset();
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterCausticsRayTracingManager: initialized successfully.");
        return true;
    }

    bool WaterCausticsRayTracingManager::EnsureConstantBuffer()
    {
        return EnsureSurfaceConstantBuffer(GetSurfaceConstantBufferSize(), "WaterCausticsRayTracingManager");
    }

    bool WaterCausticsRayTracingManager::EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex)
    {
        return EnsureOutputTextureBase(
            width,
            height,
            viewIndex,
            "WaterCausticsRayTracingManager",
            "RTWaterCausticsUAV",
            "RTWaterCausticsSRV");
    }

    void WaterCausticsRayTracingManager::SetSurfaceModelProvider(const IWaterSurfaceModelProvider* provider)
    {
        SetSurfaceModelProviderBase(provider);
    }

    const IWaterSurfaceModelProvider* WaterCausticsRayTracingManager::GetSurfaceModelProvider() const
    {
        return GetSurfaceModelProviderBase();
    }

    void WaterCausticsRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        const Vector3& lightDirection,
        const WaterSurfaceData& surfaceData,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData = ResolveSurfaceDataForDispatch(
            surfaceData,
            resolvedSurfaceData,
            "WaterCausticsRayTracingManager");

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        lastDiagnostics_ = {};
        lastDiagnostics_.viewId = viewId;
        lastDiagnostics_.waterHeight = dispatchSurfaceData.waterHeight;
        lastDiagnostics_.activeWaveCount = dispatchSurfaceData.activeWaveCount;
        lastDiagnostics_.width = width;
        lastDiagnostics_.height = height;
        lastDiagnostics_.worldPositionSrv = worldPositionSRV.ptr;
        lastDiagnostics_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;

        DispatchGuardStatus guardStatus = DispatchGuardStatus::Ok;
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
            "WaterCausticsRayTracingManager",
            "RTWaterCausticsUAV",
            "RTWaterCausticsSRV",
            guardStatus,
            outputSrvHandle,
            outputUavHandle,
            outputResource,
            outputCurrentState,
            cmdList4)) {
            switch (guardStatus) {
            case DispatchGuardStatus::NotInitialized:
                lastDiagnostics_.status = DispatchStatus::NotInitialized;
                break;
            case DispatchGuardStatus::RayTracingUnsupported:
                lastDiagnostics_.status = DispatchStatus::RayTracingUnsupported;
                break;
            case DispatchGuardStatus::NoBLAS:
                lastDiagnostics_.status = DispatchStatus::NoBLAS;
                break;
            case DispatchGuardStatus::InvalidCommandList:
            case DispatchGuardStatus::CommandList4Unavailable:
                lastDiagnostics_.status = DispatchStatus::CommandList4Unavailable;
                break;
            case DispatchGuardStatus::OutputAllocationFailed:
                lastDiagnostics_.status = DispatchStatus::OutputAllocationFailed;
                break;
            default:
                lastDiagnostics_.status = DispatchStatus::RayTracingUnsupported;
                break;
            }
            return;
        }
        lastDiagnostics_.outputSrv = outputSrvHandle.ptr;

        WaterCausticsConstants constants{};
        constants.maxTraceDistance = settings_.maxTraceDistance;
        constants.surfaceBias = settings_.surfaceBias;
        constants.intensityScale = settings_.intensityScale;
        constants.waterHeight = dispatchSurfaceData.waterHeight;
        constants.lightDirection[0] = lightDirection.x;
        constants.lightDirection[1] = lightDirection.y;
        constants.lightDirection[2] = lightDirection.z;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);

        const WaterSurfaceConstants surfaceConstants = BuildSurfaceConstants(dispatchSurfaceData);
        UploadSurfaceConstants(surfaceConstants, "WaterCausticsRayTracingManager");

        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, outputResource, *outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gCausticsOutput")),
            outputUavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWorldPosition")),
            worldPositionSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gNormalRoughness")),
            normalRoughnessSRV);
        cmdList->SetComputeRootConstantBufferView(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWaterSurfaceData")),
            constantBuffer_->GetGPUVirtualAddress());
        cmdList->SetComputeRoot32BitConstants(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("WaterCausticsConstants")),
            sizeof(WaterCausticsConstants) / sizeof(uint32_t),
            &constants,
            0);

        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        cmdList4->DispatchRays(&dispatchDesc);
        lastDiagnostics_.status = DispatchStatus::Dispatched;

        EndOutputWrite(
            cmdList,
            outputResource,
            *outputCurrentState,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterCausticsRayTracingManager: dispatch completed. status={} viewId={} width={} height={} waterHeight={:.3f} activeWaveCount={} outputSRV=0x{:X}",
            ToString(lastDiagnostics_.status),
            viewIndex,
            width,
            height,
            dispatchSurfaceData.waterHeight,
            dispatchSurfaceData.activeWaveCount,
            outputSrvHandle.ptr);
    }

    void WaterCausticsRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        ReleaseOutputIfSizeMismatchBase(width, height, viewIndex);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterCausticsRayTracingManager::GetCausticsSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    ID3D12Resource* WaterCausticsRayTracingManager::GetCausticsResource(ViewID viewId) const
    {
        return GetOutputResourceBase(static_cast<uint32_t>(viewId));
    }

    D3D12_RESOURCE_STATES& WaterCausticsRayTracingManager::GetCausticsCurrentState(ViewID viewId)
    {
        return GetOutputCurrentStateBase(static_cast<uint32_t>(viewId));
    }
}
