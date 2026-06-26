#include "pch.h"
#include "WaterCausticsRayTracingManager.h"

#include <algorithm>
#include <array>
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

        struct alignas(16) WaterCausticsSurfaceConstants {
            float waterHeight;
            uint32_t activeWaveCount;
            float time;
            float padding;
            WaterWaveParam waves[kMaxWaterSurfaceWaveCount];
        };

        constexpr UINT kWaterSurfaceCBSize =
            (sizeof(WaterCausticsSurfaceConstants) + 255u) & ~255u;

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
        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        asMgr_ = asMgr;

        if (!dxCommon_ || !descriptorManager_ || !asMgr_ || !asMgr_->IsSupported()) {
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
                "WaterCausticsRayTracingManager: constant buffer allocation failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_RANGE readRange = { 0, 0 };
        hr = constantBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&constantBufferMapped_));
        if (FAILED(hr) || !constantBufferMapped_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "WaterCausticsRayTracingManager: constant buffer map failed. hr={:#x}",
                static_cast<uint32_t>(hr));
            constantBuffer_.Reset();
            constantBufferMapped_ = nullptr;
            return false;
        }

        std::memset(constantBufferMapped_, 0, kWaterSurfaceCBSize);
        return true;
    }

    bool WaterCausticsRayTracingManager::EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex)
    {
        auto& view = views_[viewIndex];
        if (view.texture && view.width == width && view.height == height) {
            return true;
        }

        view.texture.Reset();
        view.uavHandle = {};
        view.uavCpuHandle = {};
        view.srvHandle = {};
        view.srvCpuHandle = {};
        view.width = width;
        view.height = height;
        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

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
                "WaterCausticsRayTracingManager: output texture allocation failed. viewIndex={} width={} height={} hr={:#x}",
                viewIndex,
                width,
                height,
                static_cast<uint32_t>(hr));
            return false;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = texDesc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        descriptorManager_->CreateUAV(
            view.texture.Get(),
            uavDesc,
            view.uavCpuHandle,
            view.uavHandle,
            "RTWaterCausticsUAV");

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        descriptorManager_->CreateSRV(
            view.texture.Get(),
            srvDesc,
            view.srvCpuHandle,
            view.srvHandle,
            "RTWaterCausticsSRV");

        return true;
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
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        lastDiagnostics_ = {};
        lastDiagnostics_.viewId = viewId;
        lastDiagnostics_.waterHeight = surfaceData.waterHeight;
        lastDiagnostics_.activeWaveCount = surfaceData.activeWaveCount;
        lastDiagnostics_.width = width;
        lastDiagnostics_.height = height;
        lastDiagnostics_.worldPositionSrv = worldPositionSRV.ptr;
        lastDiagnostics_.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;

        if (!isInitialized_) {
            lastDiagnostics_.status = DispatchStatus::NotInitialized;
            return;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            lastDiagnostics_.status = DispatchStatus::RayTracingUnsupported;
            return;
        }
        if (lastDiagnostics_.blasCount == 0) {
            lastDiagnostics_.status = DispatchStatus::NoBLAS;
            return;
        }
        if (!cmdList || !EnsureOutputTexture(width, height, viewIndex) || !EnsureConstantBuffer()) {
            lastDiagnostics_.status = DispatchStatus::OutputAllocationFailed;
            return;
        }

        auto& view = views_[viewIndex];
        lastDiagnostics_.outputSrv = view.srvHandle.ptr;

        WaterCausticsConstants constants{};
        constants.maxTraceDistance = settings_.maxTraceDistance;
        constants.surfaceBias = settings_.surfaceBias;
        constants.intensityScale = settings_.intensityScale;
        constants.waterHeight = surfaceData.waterHeight;
        constants.lightDirection[0] = lightDirection.x;
        constants.lightDirection[1] = lightDirection.y;
        constants.lightDirection[2] = lightDirection.z;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);

        WaterCausticsSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            lastDiagnostics_.status = DispatchStatus::CommandList4Unavailable;
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "WaterCausticsRayTracingManager: QueryInterface(ID3D12GraphicsCommandList4) failed. status={}",
                ToString(lastDiagnostics_.status));
            return;
        }

        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        ResourceBarrierHelper::Transition(
            cmdList,
            view.texture.Get(),
            view.currentState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gCausticsOutput")),
            view.uavHandle);
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

        ResourceBarrierHelper::UAV(cmdList, view.texture.Get());

        ResourceBarrierHelper::Transition(
            cmdList,
            view.texture.Get(),
            view.currentState,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        view.currentState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterCausticsRayTracingManager: dispatch completed. status={} viewId={} width={} height={} waterHeight={:.3f} activeWaveCount={} outputSRV=0x{:X}",
            ToString(lastDiagnostics_.status),
            viewIndex,
            width,
            height,
            surfaceData.waterHeight,
            surfaceData.activeWaveCount,
            view.srvHandle.ptr);
    }

    void WaterCausticsRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        auto& view = views_[viewIndex];
        if (view.width == width && view.height == height) {
            return;
        }

        view.texture.Reset();
        view.uavHandle = {};
        view.uavCpuHandle = {};
        view.srvHandle = {};
        view.srvCpuHandle = {};
        view.width = width;
        view.height = height;
        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterCausticsRayTracingManager::GetCausticsSRVHandle(ViewID viewId) const
    {
        return views_[static_cast<uint32_t>(viewId)].srvHandle;
    }

    ID3D12Resource* WaterCausticsRayTracingManager::GetCausticsResource(ViewID viewId) const
    {
        return views_[static_cast<uint32_t>(viewId)].texture.Get();
    }

    D3D12_RESOURCE_STATES& WaterCausticsRayTracingManager::GetCausticsCurrentState(ViewID viewId)
    {
        return views_[static_cast<uint32_t>(viewId)].currentState;
    }
}
