#include "pch.h"
#include "WaterCausticsRayTracingManager.h"

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct WaterCausticsConstants {
            float maxTraceDistance;
            float surfaceBias;
            float intensityScale;
            float refractiveIndex;
            float lightDirection[3];
            float screenWidth;
            float screenHeight;
            float debugDisplayScale;
            uint32_t debugViewMode;
            uint32_t lightEnabled;
            float lightColor[3];
            float lightIntensity;
            // 水面メッシュのワールドXZ範囲（RTWaterCaustics.hlsl の cbuffer 末尾と一致させること）。
            // regionValid == 0 なら範囲制限なし
            float regionCenterXZ[2];
            float regionHalfExtentXZ[2];
            // 波長依存の吸収係数 σa [1/m]（RGB）。水面描画の WaterFrameConstants と同じ値
            float absorptionCoeff[3];
            uint32_t regionValid;
            Matrix4x4 invViewProj; // 深度復元用
            // 材質の表（kRTHitNoTexture = アルファを調べず、どの物も不透明として当てる）
            uint32_t hitInstanceTableIndex;
            uint32_t hitSubMeshTableIndex;
            uint32_t pad0;
            uint32_t pad1;
        };

        static constexpr Cb::Field kWaterCausticsConstantsFields[] = {
            CB_FIELD(WaterCausticsConstants, maxTraceDistance), CB_FIELD(WaterCausticsConstants, surfaceBias),
            CB_FIELD(WaterCausticsConstants, intensityScale), CB_FIELD(WaterCausticsConstants, refractiveIndex),
            CB_FIELD(WaterCausticsConstants, lightDirection), CB_FIELD(WaterCausticsConstants, screenWidth),
            CB_FIELD(WaterCausticsConstants, screenHeight), CB_FIELD(WaterCausticsConstants, debugDisplayScale),
            CB_FIELD(WaterCausticsConstants, debugViewMode), CB_FIELD(WaterCausticsConstants, lightEnabled),
            CB_FIELD(WaterCausticsConstants, lightColor), CB_FIELD(WaterCausticsConstants, lightIntensity),
            CB_FIELD(WaterCausticsConstants, regionCenterXZ), CB_FIELD(WaterCausticsConstants, regionHalfExtentXZ),
            CB_FIELD(WaterCausticsConstants, absorptionCoeff), CB_FIELD(WaterCausticsConstants, regionValid),
            CB_FIELD(WaterCausticsConstants, invViewProj),
            CB_FIELD(WaterCausticsConstants, hitInstanceTableIndex), CB_FIELD(WaterCausticsConstants, hitSubMeshTableIndex),
            CB_FIELD(WaterCausticsConstants, pad0), CB_FIELD(WaterCausticsConstants, pad1),
        };
        CB_VERIFY_LAYOUT(WaterCausticsConstants, kWaterCausticsConstantsFields);
        CB_BIND_HLSL(WaterCausticsConstants, kWaterCausticsConstantsFields, "WaterCausticsConstants");
    }

    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");
    static_assert(sizeof(WaterCausticsConstants) == 176,
        "WaterCausticsConstants size mismatch with HLSL cbuffer");
    // 個別フィールドの境界チェックはフィールド表（下）が全フィールド分やるので不要

    bool WaterCausticsRayTracingManager::Initialize(const RTEffectInitContext& init)
    {
        // パイプラインの差分（シェーダー・エントリ名・SRV 名・定数サイズ）だけを記述する。
        // ルートシグネチャ構築〜シェーダーテーブルまでの手順は 3 マネージャ共通で
        // InitializeFromDesc（WaterRayTracingPassBase）が担う。
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterCausticsRayTracingManager";
        desc.outputDebugName = "RTWaterCaustics";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterCaustics.hlsl";
        desc.rayGenName = L"RTWaterCausticsRayGen";
        desc.outputUavName = "gCausticsOutput";
        static constexpr const char* kSrvTableNames[] = {
            "gSceneDepth", "gNormalRoughness", "gFFTOceanDisplacement", "gFFTOceanNormal" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterCausticsConstants";
        desc.constantsBytes = sizeof(WaterCausticsConstants);
        // 水面の共通関数（RTWaterSurfaceCommon.hlsli）が海の範囲マップをヒープから引く
        desc.directlyIndexedHeap = true;
        desc.rayRecordLabel = "水のコースティクス";
        return InitializeFromDesc(init, desc);
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
        ViewID viewId,
        const RTHitTableIndices& hitTables)
    {
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, width, height, surfaceData, sceneDepthSRV, {});

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
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;
        constants.refractiveIndex = settings_.refractiveIndex;
        constants.lightEnabled = lightInput.enabled ? 1u : 0u;
        constants.lightColor[0] = lightInput.color.x;
        constants.lightColor[1] = lightInput.color.y;
        constants.lightColor[2] = lightInput.color.z;
        constants.lightIntensity = lightInput.intensity;
        constants.regionCenterXZ[0] = surfaceData.regionCenterXZ[0];
        constants.regionCenterXZ[1] = surfaceData.regionCenterXZ[1];
        constants.regionHalfExtentXZ[0] = surfaceData.regionHalfExtentXZ[0];
        constants.regionHalfExtentXZ[1] = surfaceData.regionHalfExtentXZ[1];
        constants.regionValid = surfaceData.regionValid;
        constants.absorptionCoeff[0] = settings_.absorptionCoeff[0];
        constants.absorptionCoeff[1] = settings_.absorptionCoeff[1];
        constants.absorptionCoeff[2] = settings_.absorptionCoeff[2];
        constants.invViewProj = invViewProj;
        constants.hitInstanceTableIndex = hitTables.IsValid() ? hitTables.instanceTable : kRTHitNoTexture;
        constants.hitSubMeshTableIndex = hitTables.IsValid() ? hitTables.subMeshTable : kRTHitNoTexture;

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

        const SurfaceConstantsUpload surface = UploadSurfaceDataForDispatch(surfaceData, fftOceanInput);

        BindAndDispatchRays(
            cmdList,
            resources,
            surface.address,
            {
                { "gSceneDepth", sceneDepthSRV },
                { "gNormalRoughness", normalRoughnessSRV },
                { "gFFTOceanDisplacement", fftDisplacementSRV },
                { "gFFTOceanNormal", fftNormalSRV },
            },
            &constants,
            width,
            height,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // 完了ログは UI の「RTログを有効にする」でのみ出す（以前は毎フレーム無条件だった）
        if (settings_.debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsRayTracingManager: dispatch completed. status={} viewId={} size={}x{} "
                "waterHeight={:.3f} simulationType={} activeWaveCount={} outputSRV=0x{:X} debug(mode={} scale={:.3f})",
                ToString(lastDispatchInfo_.status),
                viewIndex,
                width,
                height,
                surfaceData.waterHeight,
                surfaceData.simulationType,
                surfaceData.activeWaveCount,
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

    GpuResource& WaterCausticsRayTracingManager::GetCausticsResource(ViewID viewId)
    {
        return GetOutputBase(static_cast<uint32_t>(viewId));
    }
}
