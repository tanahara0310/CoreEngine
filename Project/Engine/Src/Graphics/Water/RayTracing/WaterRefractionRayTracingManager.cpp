#include "pch.h"
#include "WaterRefractionRayTracingManager.h"

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
        struct WaterRefractionConstants {
            Matrix4x4 viewProjection;
            Matrix4x4 invViewProjection; // WorldPosition ターゲット廃止に伴う深度復元用
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
            float fftOceanPad0; // 旧 patchLength（形骸）。HLSL 側とレイアウト一致のため残す
            uint32_t fftOceanResolution;
            float debugDisplayScale;
            uint32_t debugViewMode;
        };

    }

    static_assert(sizeof(WaterRefractionConstants) == 192,
        "WaterRefractionConstants size mismatch with HLSL cbuffer");
    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");

    bool WaterRefractionRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(dxCommon, descriptorManager, asMgr,
            "WaterRefractionRayTracingManager", "RTWaterRefraction")) {
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
            .AddSRVTable("gSceneDepth", 1)
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

    void WaterRefractionRayTracingManager::Dispatch(
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

        WaterRefractionConstants constants{};
        constants.viewProjection = viewProjection;
        constants.invViewProjection = MathCore::Matrix::Inverse(viewProjection);
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
        constants.fftOceanPad0 = 0.0f;
        constants.fftOceanResolution = fftOceanInput.resolution;
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;

        const WaterSurfaceConstants surfaceConstants = UploadSurfaceDataForDispatch(dispatchSurfaceData);

        // 診断ログは UI の「RT屈折ログを有効にする」でのみ出す。
        // 以前は毎フレーム無条件に 3 本（うち 1 本は引数 26 個）流れており、
        // 他のログを埋めるうえに文字列整形のコストも常時かかっていた。
        if (settings_.debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: dispatch. viewId={} size={}x{} blas={} waterHeight={:.3f} "
                "simulationType={} activeWaveCount={} waveTime={:.3f} eta={:.4f} maxRayDistance={:.3f} "
                "surfaceBias={:.4f} maxOffsetPx={:.3f} fft(enabled={} resolution={}) "
                "srv(depth=0x{:X} color=0x{:X} out=0x{:X}) debug(mode={} scale={:.3f})",
                viewIndex,
                width,
                height,
                lastDispatchInfo_.blasCount,
                dispatchSurfaceData.waterHeight,
                dispatchSurfaceData.simulationType,
                surfaceConstants.activeWaveCount,
                dispatchSurfaceData.time,
                constants.refractionEta,
                constants.maxRayDistance,
                constants.surfaceBias,
                constants.maxRefractionOffsetPixels,
                constants.fftOceanEnabled,
                constants.fftOceanResolution,
                sceneDepthSRV.ptr,
                sceneColorSRV.ptr,
                resources.outputSrvHandle.ptr,
                constants.debugViewMode,
                constants.debugDisplayScale);
        }

        resources.cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        resources.cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, resources.outputResource, *resources.outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gRefractionOutput")),
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
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("WaterRefractionConstants")),
            sizeof(WaterRefractionConstants) / sizeof(uint32_t),
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
