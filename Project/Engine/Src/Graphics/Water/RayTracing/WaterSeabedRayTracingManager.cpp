#include "pch.h"
#include "WaterSeabedRayTracingManager.h"

#include <algorithm>
#include <cmath>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTInstanceMask.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Water/WaterCVars.h"

namespace CoreEngine
{
    namespace {
        struct WaterSeabedConstants {
            float windowOriginXZ[2];
            float texelSize;
            float maxDepth;
            uint32_t instanceMask;
            uint32_t rowOffset; ///< 測る最初の行
            uint32_t rowStride; ///< 測る行の間隔
            float pad;
        };

        static constexpr Cb::Field kWaterSeabedConstantsFields[] = {
            CB_FIELD(WaterSeabedConstants, windowOriginXZ), CB_FIELD(WaterSeabedConstants, texelSize),
            CB_FIELD(WaterSeabedConstants, maxDepth), CB_FIELD(WaterSeabedConstants, instanceMask),
            CB_FIELD(WaterSeabedConstants, rowOffset), CB_FIELD(WaterSeabedConstants, rowStride),
            CB_FIELD(WaterSeabedConstants, pad),
        };
        CB_VERIFY_LAYOUT(WaterSeabedConstants, kWaterSeabedConstantsFields);
        CB_BIND_HLSL(WaterSeabedConstants, kWaterSeabedConstantsFields, "WaterSeabedConstants");
    }

    bool WaterSeabedRayTracingManager::Initialize(const RTEffectInitContext& init)
    {
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterSeabedRayTracingManager";
        desc.outputDebugName = "RTWaterSeabed";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterSeabed.hlsl";
        desc.rayGenName = L"RTWaterSeabedRayGen";
        desc.outputUavName = "gSeabedHeightOutput";
        static constexpr const char* kSrvTableNames[] = { "gFFTOceanDisplacement" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterSeabedConstants";
        desc.constantsBytes = sizeof(WaterSeabedConstants);
        // 水面の共通関数（RTWaterSurfaceCommon.hlsli）が海の範囲マップをヒープから引く
        desc.directlyIndexedHeap = true;
        return InitializeFromDesc(init, desc);
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

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, kResolution, kResolution, surfaceData, {}, {});

        DispatchResources resources;
        if (!BeginDispatch(cmdList, kResolution, kResolution, viewIndex, resources, DXGI_FORMAT_R32_FLOAT)) {
            return;
        }

        // 範囲が動いたか出力が作り直されたフレームは全部を、それ以外は 1 組の行だけを測り直す
        const ID3D12Resource* outputResource = resources.output->Get();
        const bool fullRefresh = measuredResource_ != outputResource
            || measuredOriginX_ != window_.originX || measuredOriginZ_ != window_.originZ;
        const uint32_t refreshFrames = static_cast<uint32_t>(std::clamp(
            WaterCVars::SeabedRefreshFrames.Get(), 1, static_cast<int>(kMaxRefreshFrames)));
        uint32_t rowOffset = 0;
        uint32_t rowStride = 1;
        if (fullRefresh || refreshFrames == 1) {
            measuredResource_ = outputResource;
            measuredOriginX_ = window_.originX;
            measuredOriginZ_ = window_.originZ;
            nextRowGroup_ = 0;
        } else {
            rowOffset = nextRowGroup_ % refreshFrames;
            rowStride = refreshFrames;
            nextRowGroup_ = (rowOffset + 1) % refreshFrames;
        }
        const UINT launchHeight = (kResolution - rowOffset + rowStride - 1) / rowStride;

        WaterSeabedConstants constants{};
        constants.windowOriginXZ[0] = window_.originX;
        constants.windowOriginXZ[1] = window_.originZ;
        constants.texelSize = kTexelSize;
        constants.maxDepth = kMaxDepth;
        constants.instanceMask = RTInstanceMask::kStaticSolid | RTInstanceMask::kMovingSolid;
        constants.rowOffset = rowOffset;
        constants.rowStride = rowStride;

        const SurfaceConstantsUpload surface = UploadSurfaceDataForDispatch(surfaceData, fftOceanInput);

        BindAndDispatchRays(
            cmdList,
            resources,
            surface.address,
            {
                { "gFFTOceanDisplacement", fftOceanInput.displacementSRV },
            },
            &constants,
            kResolution,
            launchHeight,
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
