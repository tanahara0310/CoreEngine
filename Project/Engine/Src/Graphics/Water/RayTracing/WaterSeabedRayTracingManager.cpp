#include "pch.h"
#include "WaterSeabedRayTracingManager.h"

#include <cmath>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"

namespace CoreEngine
{
    namespace {
        struct WaterSeabedConstants {
            float windowOriginXZ[2];
            float texelSize;
            float maxDepth;
            uint32_t instanceMask;
            float pad[3];
        };

        static constexpr Cb::Field kWaterSeabedConstantsFields[] = {
            CB_FIELD(WaterSeabedConstants, windowOriginXZ), CB_FIELD(WaterSeabedConstants, texelSize),
            CB_FIELD(WaterSeabedConstants, maxDepth), CB_FIELD(WaterSeabedConstants, instanceMask),
            CB_FIELD(WaterSeabedConstants, pad),
        };
        CB_VERIFY_LAYOUT(WaterSeabedConstants, kWaterSeabedConstantsFields);
        CB_BIND_HLSL(WaterSeabedConstants, kWaterSeabedConstantsFields, "WaterSeabedConstants");
    }

    bool WaterSeabedRayTracingManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr,
        ShaderProgramCache* shaderProgramCache)
    {
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterSeabedRayTracingManager";
        desc.outputDebugName = "RTWaterSeabed";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterSeabed.hlsl";
        desc.rayGenName = L"RTWaterSeabedRayGen";
        desc.missName = L"RTWaterSeabedMiss";
        desc.hitGroupName = L"RTWaterSeabedHitGroup";
        desc.closestHitName = L"RTWaterSeabedClosestHit";
        desc.outputUavName = "gSeabedHeightOutput";
        static constexpr const char* kSrvTableNames[] = { "gFFTOceanDisplacement" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterSeabedConstants";
        desc.constantsBytes = sizeof(WaterSeabedConstants);
        return InitializeFromDesc(dxCommon, descriptorAllocator, asMgr, shaderProgramCache, desc);
    }

    const WaterSeabedWindow& WaterSeabedRayTracingManager::UpdateWindow(float cameraX, float cameraZ)
    {
        if (!IsInitialized()) {
            window_.valid = false;
            return window_;
        }
        const float size = static_cast<float>(kResolution) * kTexelSize;
        window_.originX = std::floor((cameraX - 0.5f * size) / kWindowSnapMeters) * kWindowSnapMeters;
        window_.originZ = std::floor((cameraZ - 0.5f * size) / kWindowSnapMeters) * kWindowSnapMeters;
        window_.size = size;
        window_.valid = true;
        return window_;
    }

    void WaterSeabedRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput,
        ViewID viewId)
    {
        if (!window_.valid) {
            return;
        }

        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData =
            ResolveSurfaceDataForDispatch(surfaceData, resolvedSurfaceData);

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, kResolution, kResolution, dispatchSurfaceData, {}, {});

        DispatchResources resources;
        if (!BeginDispatch(cmdList, kResolution, kResolution, viewIndex, resources, DXGI_FORMAT_R32_FLOAT)) {
            return;
        }

        WaterSeabedConstants constants{};
        constants.windowOriginXZ[0] = window_.originX;
        constants.windowOriginXZ[1] = window_.originZ;
        constants.texelSize = kTexelSize;
        constants.maxDepth = kMaxDepth;
        constants.instanceMask = RayTracingInstanceMask::kSolid;

        const SurfaceConstantsUpload surface = UploadSurfaceDataForDispatch(dispatchSurfaceData, fftOceanInput);

        BindAndDispatchRays(
            cmdList,
            resources,
            surface.address,
            {
                { "gFFTOceanDisplacement", fftOceanInput.displacementSRV },
            },
            &constants,
            kResolution,
            kResolution,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterSeabedRayTracingManager::GetSeabedSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    GpuResource& WaterSeabedRayTracingManager::GetSeabedResource(ViewID viewId)
    {
        return GetOutputBase(static_cast<uint32_t>(viewId));
    }
}
