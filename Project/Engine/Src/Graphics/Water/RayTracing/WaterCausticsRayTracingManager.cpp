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
            // 旧 waterHeight。水面高さは WaterSurfaceData 側（b1）に一本化され未使用。
            // 削除すると後続 float3 が 16B 境界をまたぎ HLSL とずれるためスロットは残す
            float padding0;
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
            // 水面メッシュのワールドXZ範囲（RTWaterCaustics.hlsl の cbuffer 末尾と一致させること）。
            // regionValid == 0 なら範囲制限なし
            float regionCenterXZ[2];
            float regionHalfExtentXZ[2];
            uint32_t regionValid;
            // 波長依存の吸収係数 σa [1/m]（RGB）。水面描画の WaterFrameConstants と同値を同期
            float absorptionCoeff[3];
            Matrix4x4 invViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
        };

    }

    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");
    static_assert(sizeof(WaterCausticsConstants) == 176,
        "WaterCausticsConstants size mismatch with HLSL cbuffer");
    static_assert(offsetof(WaterCausticsConstants, absorptionCoeff) / 16 == (offsetof(WaterCausticsConstants, absorptionCoeff) + 11) / 16,
        "absorptionCoeff (float3) must not straddle a 16-byte boundary");
    static_assert(offsetof(WaterCausticsConstants, invViewProj) % 16 == 0,
        "invViewProj must start on a 16-byte boundary");

    bool WaterCausticsRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        if (!InitializeBase(dxCommon, descriptorManager, asMgr,
            "WaterCausticsRayTracingManager", "RTWaterCaustics")) {
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


    void WaterCausticsRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        const LightInput& lightInput,
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput,
        const Matrix4x4& invViewProj,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData =
            ResolveSurfaceDataForDispatch(surfaceData, resolvedSurfaceData);

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, width, height, dispatchSurfaceData, sceneDepthSRV, {});

        DispatchResources resources;
        if (!BeginDispatch(cmdList, width, height, viewIndex, resources)) {
            return;
        }

        WaterCausticsConstants constants{};
        constants.maxTraceDistance = settings_.maxTraceDistance;
        constants.surfaceBias = settings_.surfaceBias;
        constants.intensityScale = settings_.intensityScale;
        constants.lightDirection[0] = lightInput.direction.x;
        constants.lightDirection[1] = lightInput.direction.y;
        constants.lightDirection[2] = lightInput.direction.z;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.fftOceanEnabled = fftOceanInput.enabled;
        constants.fftOceanPatchLength = fftOceanInput.patchLength;
        constants.fftOceanResolution = fftOceanInput.resolution;
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
        constants.absorptionCoeff[0] = settings_.absorptionCoeff[0];
        constants.absorptionCoeff[1] = settings_.absorptionCoeff[1];
        constants.absorptionCoeff[2] = settings_.absorptionCoeff[2];
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

        UploadSurfaceDataForDispatch(dispatchSurfaceData);

        resources.cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        resources.cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, resources.outputResource, *resources.outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gCausticsOutput")),
            resources.outputUavHandle);
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
        resources.cmdList4->DispatchRays(&dispatchDesc);
        lastDiagnostics_.status = DispatchStatus::Dispatched;

        EndOutputWrite(
            cmdList,
            resources.outputResource,
            *resources.outputCurrentState,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // 完了ログは UI の「RTログを有効にする」でのみ出す（以前は毎フレーム無条件だった）
        if (settings_.debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: dispatch completed. status={} viewId={} size={}x{} "
                "waterHeight={:.3f} simulationType={} activeWaveCount={} outputSRV=0x{:X} debug(mode={} scale={:.3f})",
                ToString(lastDiagnostics_.status),
                viewIndex,
                width,
                height,
                dispatchSurfaceData.waterHeight,
                dispatchSurfaceData.simulationType,
                dispatchSurfaceData.activeWaveCount,
                resources.outputSrvHandle.ptr,
                settings_.debugViewMode,
                settings_.debugDisplayScale);
        }
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
