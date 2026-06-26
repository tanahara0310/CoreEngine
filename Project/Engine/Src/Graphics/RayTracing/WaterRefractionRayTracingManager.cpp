#include "pch.h"
#include "WaterRefractionRayTracingManager.h"

#include <algorithm>
#include <cstring>

#include "AccelerationStructureManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct alignas(16) WaterRefractionConstants {
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
            float padding;
        };

        struct alignas(16) WaterRefractionSurfaceConstants {
            float waterHeight;
            uint32_t activeWaveCount;
            float time;
            float padding;
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount];
        };

        constexpr UINT kWaterSurfaceCBSize =
            (sizeof(WaterRefractionSurfaceConstants) + 255u) & ~255u;

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

    static_assert(sizeof(WaterRefractionConstants) == 112,
        "WaterRefractionConstants size mismatch with HLSL cbuffer");
    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");

    bool WaterRefractionRayTracingManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        asMgr_ = asMgr;

        Logger& log = Logger::GetInstance();

        if (!dxCommon_ || !descriptorManager_ || !asMgr_ || !asMgr_->IsSupported()) {
            log.Log("WaterRefractionRayTracingManager: DXR not supported, skipping",
                LogLevel::Warn, LogCategory::Graphics);
            return false;
        }

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        shaderBlob_.Attach(shaderCompiler.CompileShaderLibrary(L"Application/Assets/Shaders/Water/RTWaterRefraction.hlsl"));
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
        if (constantBuffer_) {
            return true;
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kWaterSurfaceCBSize;
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
            IID_PPV_ARGS(&constantBuffer_));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "WaterRefractionRayTracingManager: constant buffer allocation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange = { 0, 0 };
        hr = constantBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&constantBufferMapped_));
        if (FAILED(hr) || !constantBufferMapped_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "WaterRefractionRayTracingManager: constant buffer map failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            constantBuffer_.Reset();
            constantBufferMapped_ = nullptr;
            return false;
        }

        std::memset(constantBufferMapped_, 0, kWaterSurfaceCBSize);
        return true;
    }

    bool WaterRefractionRayTracingManager::EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex)
    {
        auto& view = views_[viewIndex];
        if (view.texture && view.width == width && view.height == height) {
            return true;
        }

        view.width = width;
        view.height = height;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&view.texture));
        if (FAILED(hr)) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "WaterRefractionRayTracingManager: output texture allocation failed. viewIndex={} width={} height={} hr={:#x}",
                viewIndex,
                width,
                height,
                static_cast<uint32_t>(hr));
            return false;
        }

        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        descriptorManager_->CreateUAV(
            view.texture.Get(),
            uavDesc,
            view.uavCpuHandle,
            view.uavHandle,
            "RTWaterRefraction_UAV_v" + std::to_string(viewIndex));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        descriptorManager_->CreateSRV(
            view.texture.Get(),
            srvDesc,
            view.srvCpuHandle,
            view.srvHandle,
            "RTWaterRefraction_SRV_v" + std::to_string(viewIndex));

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::RenderTarget,
            "WaterRefractionRayTracingManager: output texture ready. viewIndex={} width={} height={} uav=0x{:X} srv=0x{:X}",
            viewIndex,
            width,
            height,
            view.uavHandle.ptr,
            view.srvHandle.ptr);

        return true;
    }

    void WaterRefractionRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        EnsureOutputTexture(width, height, static_cast<uint32_t>(viewId));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterRefractionRayTracingManager::GetRefractionSRVHandle(ViewID viewId) const
    {
        return views_[static_cast<uint32_t>(viewId)].srvHandle;
    }

    ID3D12Resource* WaterRefractionRayTracingManager::GetRefractionResource(ViewID viewId) const
    {
        return views_[static_cast<uint32_t>(viewId)].texture.Get();
    }

    D3D12_RESOURCE_STATES& WaterRefractionRayTracingManager::GetRefractionCurrentState(ViewID viewId)
    {
        return views_[static_cast<uint32_t>(viewId)].currentState;
    }

    void WaterRefractionRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const WaterSurfaceData& surfaceData,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        lastDiagnostics_ = {};
        lastDiagnostics_.viewId = viewId;
        lastDiagnostics_.waterHeight = surfaceData.waterHeight;
        lastDiagnostics_.width = width;
        lastDiagnostics_.height = height;
        lastDiagnostics_.worldPositionSrv = worldPositionSRV.ptr;
        lastDiagnostics_.sceneColorSrv = sceneColorSRV.ptr;
        lastDiagnostics_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;

        if (!isInitialized_ || !cmdList || !asMgr_ || !asMgr_->IsSupported()) {
            lastDiagnostics_.status = !isInitialized_
                ? WaterRefractionRayTracingManager::DispatchStatus::NotInitialized
                : WaterRefractionRayTracingManager::DispatchStatus::RayTracingUnsupported;
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
        if (asMgr_->GetBLASCount() == 0) {
            lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::NoBLAS;
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: dispatch skipped. status={} viewId={} waterHeight={:.3f}",
                ToString(lastDiagnostics_.status),
                static_cast<uint32_t>(viewId),
                surfaceData.waterHeight);
            return;
        }

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        auto& view = views_[viewIndex];
        if (!EnsureOutputTexture(width, height, viewIndex)) {
            lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::OutputAllocationFailed;
            return;
        }
        if (!EnsureConstantBuffer()) {
            lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::OutputAllocationFailed;
            return;
        }
        lastDiagnostics_.outputSrv = view.srvHandle.ptr;

        WaterRefractionConstants constants{};
        constants.viewProjection = viewProjection;
        constants.cameraPosition[0] = cameraPosition.x;
        constants.cameraPosition[1] = cameraPosition.y;
        constants.cameraPosition[2] = cameraPosition.z;
        constants.waterHeight = surfaceData.waterHeight;
        constants.surfaceBias = settings_.surfaceBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        constants.refractionEta = 1.0f / (std::max)(settings_.waterRefractiveIndex, 1.0e-4f);
        constants.absorptionCoeff = settings_.absorptionCoeff;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.maxRefractionOffsetPixels = settings_.maxRefractionOffsetPixels;

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            lastDiagnostics_.status = WaterRefractionRayTracingManager::DispatchStatus::CommandList4Unavailable;
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "WaterRefractionRayTracingManager: QueryInterface(ID3D12GraphicsCommandList4) failed. status={}",
                ToString(lastDiagnostics_.status));
            return;
        }

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterRefractionRayTracingManager: dispatch begin. viewId={} waterHeight={:.3f} width={} height={} blasCount={} worldPosSRV=0x{:X} sceneColorSRV=0x{:X} outputSRV=0x{:X} eta={:.4f} maxRayDistance={:.3f} absorptionCoeff={:.3f} surfaceBias={:.4f} maxOffsetPx={:.3f} cameraPos=({:.3f}, {:.3f}, {:.3f}) activeWaveCount={} waveTime={:.3f}",
            viewIndex,
            surfaceData.waterHeight,
            width,
            height,
            lastDiagnostics_.blasCount,
            worldPositionSRV.ptr,
            sceneColorSRV.ptr,
            view.srvHandle.ptr,
            constants.refractionEta,
            constants.maxRayDistance,
            constants.absorptionCoeff,
            constants.surfaceBias,
            constants.maxRefractionOffsetPixels,
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            surfaceData.activeWaveCount,
            surfaceData.time);

        WaterRefractionSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));

        if (surfaceConstants.activeWaveCount > 0) {
            const WaterWaveParam& firstWave = surfaceConstants.waves[0];
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRefractionRayTracingManager: surface constants uploaded. waterHeight={:.3f} activeWaveCount={} waveTime={:.3f} firstWave(dir=({:.3f}, {:.3f}) amp={:.4f} len={:.3f} speed={:.3f} steep={:.3f} phase={:.3f})",
                surfaceConstants.waterHeight,
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
                "WaterRefractionRayTracingManager: surface constants uploaded. waterHeight={:.3f} activeWaveCount=0 waveTime={:.3f}",
                surfaceConstants.waterHeight,
                surfaceConstants.time);
        }

        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        ResourceBarrierHelper::Transition(
            cmdList,
            view.texture.Get(),
            view.currentState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gRefractionOutput")),
            view.uavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWorldPosition")),
            worldPositionSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gSceneColor")),
            sceneColorSRV);
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

        ResourceBarrierHelper::UAV(cmdList, view.texture.Get());
        ResourceBarrierHelper::Transition(
            cmdList,
            view.texture.Get(),
            view.currentState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterRefractionRayTracingManager: dispatch end. status={} viewId={} outputState={}",
            ToString(lastDiagnostics_.status),
            viewIndex,
            static_cast<uint32_t>(view.currentState));
    }
}
