#include "pch.h"
#include "WaterRefractionRayTracingManager.h"

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
            // 旧 FFT 有効情報 3 スロット。実体は b1（WaterSurfaceConstants）へ一本化済み。
            // レイアウト維持のためスロットだけ残す
            uint32_t fftOceanPad1;
            float fftOceanPad0;
            uint32_t fftOceanPad2;
            float debugDisplayScale;
            uint32_t debugViewMode;
        };

        static constexpr Cb::Field kWaterRefractionConstantsFields[] = {
            CB_FIELD(WaterRefractionConstants, viewProjection),
            CB_FIELD(WaterRefractionConstants, invViewProjection),
            CB_FIELD(WaterRefractionConstants, cameraPosition), CB_FIELD(WaterRefractionConstants, waterHeight),
            CB_FIELD(WaterRefractionConstants, surfaceBias), CB_FIELD(WaterRefractionConstants, maxRayDistance),
            CB_FIELD(WaterRefractionConstants, refractionEta), CB_FIELD(WaterRefractionConstants, absorptionCoeff),
            CB_FIELD(WaterRefractionConstants, screenWidth), CB_FIELD(WaterRefractionConstants, screenHeight),
            CB_FIELD(WaterRefractionConstants, maxRefractionOffsetPixels),
            CB_FIELD(WaterRefractionConstants, fftOceanPad1), CB_FIELD(WaterRefractionConstants, fftOceanPad0),
            CB_FIELD(WaterRefractionConstants, fftOceanPad2),
            CB_FIELD(WaterRefractionConstants, debugDisplayScale),
            CB_FIELD(WaterRefractionConstants, debugViewMode),
        };
        CB_VERIFY_LAYOUT(WaterRefractionConstants, kWaterRefractionConstantsFields);
        CB_BIND_HLSL(WaterRefractionConstants, kWaterRefractionConstantsFields, "WaterRefractionConstants");
    }

    static_assert(sizeof(WaterRefractionConstants) == 192,
        "WaterRefractionConstants size mismatch with HLSL cbuffer");
    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");

    // 屈折固有の構成（シェーダーパス・バインド名・出力フォーマット）を基盤へ渡す
    bool WaterRefractionRayTracingManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr)
    {
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterRefractionRayTracingManager";
        desc.outputDebugName = "RTWaterRefraction";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterRefraction.hlsl";
        desc.rayGenName = L"RTWaterRefractionRayGen";
        desc.missName = L"RTWaterRefractionMiss";
        desc.hitGroupName = L"RTWaterRefractionHitGroup";
        desc.closestHitName = L"RTWaterRefractionClosestHit";
        desc.outputUavName = "gRefractionOutput";
        static constexpr const char* kSrvTableNames[] = {
            "gSceneDepth", "gSceneColor", "gFFTOceanDisplacement", "gFFTOceanNormal" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterRefractionConstants";
        desc.constantsBytes = sizeof(WaterRefractionConstants);
        return InitializeFromDesc(dxCommon, descriptorAllocator, asMgr, desc);
    }

    void WaterRefractionRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        ReleaseOutputIfSizeMismatchBase(width, height, static_cast<uint32_t>(viewId));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterRefractionRayTracingManager::GetRefractionSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    GpuResource& WaterRefractionRayTracingManager::GetRefractionResource(ViewID viewId)
    {
        return GetOutputBase(static_cast<uint32_t>(viewId));
    }

    // 水面から下向きに屈折レイを飛ばし、水中のシーン色を屈折テクスチャへ書く。
    // 結果を消費するのは WaterSurfacePass（GameView のみ）
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
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;

        const WaterSurfaceConstants surfaceConstants =
            UploadSurfaceDataForDispatch(dispatchSurfaceData, fftOceanInput);

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
                fftOceanInput.enabled,
                fftOceanInput.resolution,
                sceneDepthSRV.ptr,
                sceneColorSRV.ptr,
                resources.outputSrvHandle.ptr,
                constants.debugViewMode,
                constants.debugDisplayScale);
        }

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
