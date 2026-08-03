#include "pch.h"
#include "WaterReflectionRayTracingManager.h"

#include <algorithm>

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
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
            // 旧 FFT 有効情報 3 スロット。実体は b1（WaterSurfaceConstants）へ一本化済み。
            // レイアウト維持のためスロットだけ残す
            uint32_t fftOceanPad1;
            float fftOceanPad0;
            uint32_t fftOceanPad2;
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
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterReflectionRayTracingManager";
        desc.outputDebugName = "RTWaterReflection";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterReflection.hlsl";
        desc.rayGenName = L"RTWaterReflectionRayGen";
        desc.missName = L"RTWaterReflectionMiss";
        desc.hitGroupName = L"RTWaterReflectionHitGroup";
        desc.closestHitName = L"RTWaterReflectionClosestHit";
        desc.outputUavName = "gReflectionOutput";
        desc.srvTableNames = { "gSceneDepth", "gSceneColor", "gFFTOceanDisplacement", "gFFTOceanNormal" };
        desc.constantsName = "WaterReflectionConstants";
        desc.constantsBytes = sizeof(WaterReflectionConstants);
        return InitializeFromDesc(dxCommon, descriptorManager, asMgr, desc);
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
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;

        UploadSurfaceDataForDispatch(dispatchSurfaceData, fftOceanInput);

        BindAndDispatchRays(
            cmdList,
            resources,
            {
                { "gSceneDepth", sceneDepthSRV },
                { "gSceneColor", sceneColorSRV },
                { "gFFTOceanDisplacement", fftDisplacementSRV },
                { "gFFTOceanNormal", fftNormalSRV },
            },
            &constants,
            width,
            height,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
}
