#include "pch.h"
#include "WaterCausticsRayTracingManager.h"

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
        struct WaterCausticsConstants {
            float maxTraceDistance;
            float surfaceBias;
            float intensityScale;
            float waterHeight;
            float lightDirection[3];
            float screenWidth;
            float screenHeight;
            uint32_t fftOceanEnabled;
            float fftOceanPatchLength;
            uint32_t fftOceanResolution;
            float refractiveIndex;
            float debugDisplayScale;
            uint32_t debugViewMode;
            uint32_t lightEnabled; // 旧 padding を転用
            // ここまでで HLSL 側 r0〜r3 に対応（lightColor/lightIntensity が r4）
            float lightColor[3];
            float lightIntensity;
            // ワールドXZ → FFT テクスチャ UV の写像（uv = worldXZ * scale + offset）。
            // ラスタ描画（FFTWater.VS）と同じ波面を RT が評価するために必須
            float fftOceanUVScale[2];
            float fftOceanUVOffset[2];
            // 水面メッシュのワールドXZ範囲（RTWaterCaustics.hlsl の cbuffer 末尾と一致させること）。
            // regionValid == 0 なら範囲制限なし
            float regionCenterXZ[2];
            float regionHalfExtentXZ[2];
            uint32_t regionValid;
            float regionPadding[3];
            Matrix4x4 invViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
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
    static_assert(sizeof(WaterCausticsConstants) == 192,
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
        shaderBlob_.Attach(shaderCompiler.CompileShaderLibrary(L"Engine/Assets/Shaders/Water/RayTracing/RTWaterCaustics.hlsl"));
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
            .AddSRVTable("gSceneDepth", 1)
            .AddSRVTable("gNormalRoughness", 2)
            .AddSRVTable("gFFTOceanDisplacement", 3)
            .AddSRVTable("gFFTOceanNormal", 4)
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

    void WaterCausticsRayTracingManager::SetSurfaceModelProvider(const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        SetSurfaceModelProviderBase(provider);
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterCausticsRayTracingManager::GetSurfaceModelProvider() const
    {
        return GetSurfaceModelProviderBase();
    }

    void WaterCausticsRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        const LightInput& lightInput,
        const WaterSurfaceData& surfaceData,
        const FFTOceanCausticsInput& fftOceanInput,
        const Matrix4x4& invViewProj,
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
        lastDiagnostics_.worldPositionSrv = sceneDepthSRV.ptr;
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
        constants.lightDirection[0] = lightInput.direction.x;
        constants.lightDirection[1] = lightInput.direction.y;
        constants.lightDirection[2] = lightInput.direction.z;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.fftOceanEnabled = fftOceanInput.enabled;
        constants.fftOceanPatchLength = fftOceanInput.patchLength;
        constants.fftOceanResolution = fftOceanInput.resolution;
        constants.fftOceanUVScale[0] = fftOceanInput.uvScale[0];
        constants.fftOceanUVScale[1] = fftOceanInput.uvScale[1];
        constants.fftOceanUVOffset[0] = fftOceanInput.uvOffset[0];
        constants.fftOceanUVOffset[1] = fftOceanInput.uvOffset[1];
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;
        constants.refractiveIndex = settings_.refractiveIndex;
        constants.lightEnabled = lightInput.enabled ? 1u : 0u;
        constants.lightColor[0] = lightInput.color.x;
        constants.lightColor[1] = lightInput.color.y;
        constants.lightColor[2] = lightInput.color.z;
        constants.lightIntensity = lightInput.intensity;
        constants.regionCenterXZ[0] = dispatchSurfaceData.regionCenterXZ[0];
        constants.regionCenterXZ[1] = dispatchSurfaceData.regionCenterXZ[1];
        constants.regionHalfExtentXZ[0] = dispatchSurfaceData.regionHalfExtentXZ[0];
        constants.regionHalfExtentXZ[1] = dispatchSurfaceData.regionHalfExtentXZ[1];
        constants.regionValid = dispatchSurfaceData.regionValid;
        constants.invViewProj = invViewProj;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : normalRoughnessSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : normalRoughnessSRV;

        if (settings_.debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: RT caustics debug active. mode={} scale={:.3f} intensityScale={:.3f} refractiveIndex={:.4f}",
                settings_.debugViewMode,
                settings_.debugDisplayScale,
                settings_.intensityScale,
                settings_.refractiveIndex);
        }

        const WaterSurfaceConstants surfaceConstants = UploadSurfaceDataForDispatch(
            dispatchSurfaceData,
            "WaterCausticsRayTracingManager");

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
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gSceneDepth")),
            sceneDepthSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gNormalRoughness")),
            normalRoughnessSRV);
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
            "WaterCausticsRayTracingManager: dispatch completed. status={} viewId={} width={} height={} waterHeight={:.3f} simulationType={} activeWaveCount={} outputSRV=0x{:X} debugViewMode={} debugScale={:.3f} debugLogEnabled={}",
            ToString(lastDiagnostics_.status),
            viewIndex,
            width,
            height,
            dispatchSurfaceData.waterHeight,
            dispatchSurfaceData.simulationType,
            dispatchSurfaceData.activeWaveCount,
            outputSrvHandle.ptr,
            settings_.debugViewMode,
            settings_.debugDisplayScale,
            settings_.debugLogEnabled);
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
