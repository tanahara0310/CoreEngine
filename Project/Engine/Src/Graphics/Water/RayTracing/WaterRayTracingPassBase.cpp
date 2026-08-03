#include "pch.h"
#include "WaterRayTracingPassBase.h"

#include <algorithm>
#include <cstring>

#include <dxcapi.h>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RayTracingPipelineBuilder.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool WaterRayTracingPassBase::InitializeFromDesc(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr,
        const RTWaterPipelineDesc& desc)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(dxCommon, descriptorManager, asMgr, desc.ownerName, desc.outputDebugName)) {
            log.Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: DXR unsupported. initialization skipped.",
                desc.ownerName);
            return false;
        }

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
        shaderBlob.Attach(shaderCompiler.CompileShaderLibrary(desc.shaderPath));
        if (!shaderBlob) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: shader compile failed.",
                desc.ownerName);
            return false;
        }

        // ルートシグネチャは全パス同型: 出力 UAV(u0) / TLAS(t0) / 追加 SRV(t1〜) /
        // 水面サーフェス CBV(b1) / 32bit root constants(b0)。
        globalRootSigMgr_.AddUAVTable(desc.outputUavName, 0);
        globalRootSigMgr_.AddSRVTable("gScene", 0);
        uint32_t srvRegister = 1;
        for (const char* srvName : desc.srvTableNames) {
            globalRootSigMgr_.AddSRVTable(srvName, srvRegister++);
        }
        globalRootSigMgr_.AddCBV("gWaterSurfaceData", 1);
        globalRootSigMgr_.AddRootConstants(desc.constantsName, 0, desc.constantsBytes / sizeof(uint32_t));
        if (!globalRootSigMgr_.Build(dxCommon_->GetDevice())) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: global root signature build failed.",
                desc.ownerName);
            return false;
        }

        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob.Get())
            .AddHitGroup({ desc.hitGroupName, desc.closestHitName })
            .SetShaderConfig(desc.payloadBytes)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        if (!pipelineBuilder.Build(dxCommon_->GetDevice(), stateObject_, stateObjectProperties_)) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: state object build failed.",
                desc.ownerName);
            return false;
        }

        shaderTableBuilder_
            .SetRayGenShader(desc.rayGenName)
            .AddMissShader(desc.missName)
            .AddHitGroup(desc.hitGroupName);
        if (!shaderTableBuilder_.Build(dxCommon_->GetDevice(), stateObjectProperties_.Get())) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: shader table build failed.",
                desc.ownerName);
            return false;
        }

        outputUavName_ = desc.outputUavName;
        constantsName_ = desc.constantsName;
        constantsBytes_ = desc.constantsBytes;

        isInitialized_ = true;
        log.Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "{}: initialized successfully.",
            desc.ownerName);
        return true;
    }

    void WaterRayTracingPassBase::BindAndDispatchRays(
        ID3D12GraphicsCommandList* cmdList,
        DispatchResources& resources,
        std::initializer_list<RTWaterSrvBinding> srvBindings,
        const void* constantsBlob,
        UINT width,
        UINT height,
        D3D12_RESOURCE_STATES finalState)
    {
        resources.cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        resources.cmdList4->SetPipelineState1(stateObject_.Get());

        BeginOutputWrite(cmdList, resources.outputResource, *resources.outputCurrentState);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex(outputUavName_)),
            resources.outputUavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        for (const RTWaterSrvBinding& binding : srvBindings) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex(binding.name)),
                binding.handle);
        }
        cmdList->SetComputeRootConstantBufferView(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWaterSurfaceData")),
            constantBuffer_->GetGPUVirtualAddress());
        cmdList->SetComputeRoot32BitConstants(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex(constantsName_)),
            constantsBytes_ / sizeof(uint32_t),
            constantsBlob,
            0);

        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        resources.cmdList4->DispatchRays(&dispatchDesc);
        lastDispatchInfo_.status = RayTracingDispatchStatus::Dispatched;

        EndOutputWrite(cmdList, resources.outputResource, *resources.outputCurrentState, finalState);
    }

    bool WaterRayTracingPassBase::BeginDispatch(
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DispatchResources& outResources,
        DXGI_FORMAT format)
    {
        return BeginDispatchBase(
            cmdList, width, height, viewIndex, outResources, format, GetSurfaceConstantBufferSize());
    }

    void WaterRayTracingPassBase::BeginDiagnostics(
        uint32_t viewIndex,
        UINT width,
        UINT height,
        const WaterSurfaceData& surfaceData,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV)
    {
        BeginDiagnosticsBase(viewIndex, width, height);

        lastWaterHeight_ = surfaceData.waterHeight;
        lastActiveWaveCount_ = surfaceData.activeWaveCount;

        // 水面固有の値は共通型の extras に載せる（デバッグ UI はパスの種類を知らずに表へ出せる）
        lastDispatchInfo_.AddExtra("waterHeight", surfaceData.waterHeight);
        lastDispatchInfo_.AddExtra("activeWaves", static_cast<float>(surfaceData.activeWaveCount));
        lastDispatchInfo_.AddExtra("sceneDepthSrv", sceneDepthSRV.ptr != 0 ? 1.0f : 0.0f);
        lastDispatchInfo_.AddExtra("sceneColorSrv", sceneColorSRV.ptr != 0 ? 1.0f : 0.0f);
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::BuildSurfaceConstants(
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput) const
    {
        WaterSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        surfaceConstants.simulationType = surfaceData.simulationType;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        surfaceConstants.fftOceanEnabled = fftOceanInput.enabled;
        surfaceConstants.fftOceanResolution = fftOceanInput.resolution;
        surfaceConstants.meshSubdivisions = surfaceData.meshSubdivisions;
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const
    {
        if (!constantBufferMapped_) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: surface constant upload skipped. constant buffer is not mapped.",
                GetOwnerName());
            return;
        }

        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));
    }

    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::UploadSurfaceDataForDispatch(
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput) const
    {
        const WaterSurfaceConstants surfaceConstants = BuildSurfaceConstants(surfaceData, fftOceanInput);
        UploadSurfaceConstants(surfaceConstants);
        return surfaceConstants;
    }

    void WaterRayTracingPassBase::SetSurfaceModelProvider(
        const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        surfaceModelProvider_ = provider;
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterRayTracingPassBase::GetSurfaceModelProvider() const
    {
        return surfaceModelProvider_.lock();
    }

    const WaterSurfaceData& WaterRayTracingPassBase::ResolveSurfaceDataForDispatch(
        const WaterSurfaceData& fallbackSurfaceData,
        WaterSurfaceData& outResolvedSurfaceData) const
    {
        const std::shared_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider = surfaceModelProvider_.lock();
        if (!surfaceModelProvider) {
            return fallbackSurfaceData;
        }

        if (!surfaceModelProvider->TryGetSurfaceData(outResolvedSurfaceData)) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: surface model provider returned no data. fallback path is used. provider='{}' type={}",
                GetOwnerName(),
                surfaceModelProvider->GetProviderName(),
                static_cast<uint32_t>(surfaceModelProvider->GetSimulationType()));
            return fallbackSurfaceData;
        }

        // 以前はここで毎フレーム Infof を出していたが、3 マネージャ分が常時流れて
        // ログを埋めるだけだったため撤去した（解決結果は GetDispatchInfo で参照できる）。
        return outResolvedSurfaceData;
    }
}
