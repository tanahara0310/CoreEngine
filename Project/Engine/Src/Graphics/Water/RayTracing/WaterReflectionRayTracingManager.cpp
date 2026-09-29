#include "pch.h"
#include "WaterReflectionRayTracingManager.h"

#include <algorithm>

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
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
            float sunDirection[3];            // メインライトの向き（光源→シーン・正規化済み）
            float surfaceBias;
            float maxRayDistance;
            // 1 = 空キューブマップが有効。RT パス側が「トレースしたレイの向き」で空を解決するのに使う
            float skyEnvReflectionEnabled;
            float screenWidth;
            float screenHeight;
            float maxReflectionOffsetPixels;
            uint32_t sunShadowEnabled;        // 1 なら水面の点から光源へ影のレイを撃つ
            float debugDisplayScale;
            uint32_t debugViewMode;
        };

        static constexpr Cb::Field kWaterReflectionConstantsFields[] = {
            CB_FIELD(WaterReflectionConstants, viewProjection),
            CB_FIELD(WaterReflectionConstants, invViewProjection),
            CB_FIELD(WaterReflectionConstants, cameraPosition), CB_FIELD(WaterReflectionConstants, waterHeight),
            CB_FIELD(WaterReflectionConstants, sunDirection), CB_FIELD(WaterReflectionConstants, surfaceBias),
            CB_FIELD(WaterReflectionConstants, maxRayDistance),
            CB_FIELD(WaterReflectionConstants, skyEnvReflectionEnabled),
            CB_FIELD(WaterReflectionConstants, screenWidth), CB_FIELD(WaterReflectionConstants, screenHeight),
            CB_FIELD(WaterReflectionConstants, maxReflectionOffsetPixels),
            CB_FIELD(WaterReflectionConstants, sunShadowEnabled),
            CB_FIELD(WaterReflectionConstants, debugDisplayScale),
            CB_FIELD(WaterReflectionConstants, debugViewMode),
        };
        CB_VERIFY_LAYOUT(WaterReflectionConstants, kWaterReflectionConstantsFields);
        CB_BIND_HLSL(WaterReflectionConstants, kWaterReflectionConstantsFields, "WaterReflectionConstants");
    }

    static_assert(sizeof(WaterReflectionConstants) == 192,
        "WaterReflectionConstants size mismatch with HLSL cbuffer");

    bool WaterReflectionRayTracingManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr,
        ShaderProgramCache* shaderProgramCache)
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
        // gSkyEnvironmentMap は t5。空をこのパス内で解決するために追加した
        // （Water.PS 側で平面法線の空と混ぜると二重像になるため）。
        static constexpr const char* kSrvTableNames[] = {
            "gSceneDepth", "gSceneColor", "gFFTOceanDisplacement", "gFFTOceanNormal", "gSkyEnvironmentMap" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterReflectionConstants";
        desc.constantsBytes = sizeof(WaterReflectionConstants);
        desc.secondaryOutputUavName = "gSunVisibilityOutput";
        return InitializeFromDesc(dxCommon, descriptorAllocator, asMgr, shaderProgramCache, desc);
    }

    void WaterReflectionRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        ReleaseOutputIfSizeMismatchBase(width, height, viewIndex);
        outputViews_.ReleaseIfSizeMismatch(width, height, kSunVisibilitySlotBase + viewIndex);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterReflectionRayTracingManager::GetSunVisibilitySRVHandle(ViewID viewId) const
    {
        return outputViews_.GetSRVHandle(kSunVisibilitySlotBase + static_cast<uint32_t>(viewId));
    }

    GpuResource& WaterReflectionRayTracingManager::GetSunVisibilityResource(ViewID viewId)
    {
        return outputViews_.Resource(kSunVisibilitySlotBase + static_cast<uint32_t>(viewId));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterReflectionRayTracingManager::GetReflectionSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    GpuResource& WaterReflectionRayTracingManager::GetReflectionResource(ViewID viewId)
    {
        return GetOutputBase(static_cast<uint32_t>(viewId));
    }

    void WaterReflectionRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput,
        D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV,
        const WaterSunShadowInput& sunShadow,
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

        // 水面の日向率（反射と同じ解像度の 1 チャンネル）
        const uint32_t sunVisibilitySlot = kSunVisibilitySlotBase + viewIndex;
        RayTracingOutputViewSet::TextureOptions sunVisibilityOptions{};
        sunVisibilityOptions.format = DXGI_FORMAT_R8_UNORM;
        if (!outputViews_.EnsureTexture(
                dxCommon_, descriptorAllocator_, width, height, sunVisibilitySlot, GetOwnerName(),
                "RTWaterSunVisibility_v" + std::to_string(viewIndex), sunVisibilityOptions)) {
            lastDispatchInfo_.status = RayTracingDispatchStatus::OutputAllocationFailed;
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: sun visibility texture allocation failed. dispatch skipped.",
                GetOwnerName());
            return;
        }

        WaterReflectionConstants constants{};
        constants.viewProjection = viewProjection;
        constants.invViewProjection = MathCore::Matrix::Inverse(viewProjection);
        constants.cameraPosition[0] = cameraPosition.x;
        constants.cameraPosition[1] = cameraPosition.y;
        constants.cameraPosition[2] = cameraPosition.z;
        constants.waterHeight = dispatchSurfaceData.waterHeight;
        constants.sunDirection[0] = sunShadow.direction.x;
        constants.sunDirection[1] = sunShadow.direction.y;
        constants.sunDirection[2] = sunShadow.direction.z;
        constants.surfaceBias = settings_.surfaceBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        // 空キューブが渡っていないフレームはシェーダー側が理由コードへ落とし、
        // Water.PS の保険フォールバック（波法線で引く空 / PBR 出力）が動く。
        const bool hasSkyEnvironment = (skyEnvironmentSRV.ptr != 0);
        constants.skyEnvReflectionEnabled = hasSkyEnvironment ? 1.0f : 0.0f;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.maxReflectionOffsetPixels = settings_.maxReflectionOffsetPixels;
        constants.sunShadowEnabled = sunShadow.enabled ? 1u : 0u;
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;
        // 未取得のときはダミーを差す（FFT と同じ規約）。実際に読むかは
        // skyEnvReflectionEnabled でシェーダー側が判断する。
        const D3D12_GPU_DESCRIPTOR_HANDLE skyEnvSRV =
            hasSkyEnvironment ? skyEnvironmentSRV : sceneColorSRV;

        UploadSurfaceDataForDispatch(dispatchSurfaceData, fftOceanInput);

        BindAndDispatchRays(
            cmdList,
            resources,
            {
                { "gSceneDepth", sceneDepthSRV },
                { "gSceneColor", sceneColorSRV },
                { "gFFTOceanDisplacement", fftDisplacementSRV },
                { "gFFTOceanNormal", fftNormalSRV },
                { "gSkyEnvironmentMap", skyEnvSRV },
            },
            &constants,
            width,
            height,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            { &outputViews_.Resource(sunVisibilitySlot), outputViews_.GetUAVHandle(sunVisibilitySlot) });
    }
}
