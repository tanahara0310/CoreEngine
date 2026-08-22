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
            // 旧 waterHeight。水面高さは WaterSurfaceData 側（b1）に一本化され未使用。
            // 削除すると後続 float3 が 16B 境界をまたぎ HLSL とずれるためスロットは残す
            float padding0;
            float lightDirection[3];
            float screenWidth;
            float screenHeight;
            // 旧 FFT 有効情報 3 スロット。実体は b1（WaterSurfaceConstants）へ一本化済み。
            // 後続の float3 の 16B 境界を守るためレイアウトだけ残す
            uint32_t fftOceanPad1;
            float fftOceanPad0;
            uint32_t fftOceanPad2;
            float refractiveIndex;
            float debugDisplayScale;
            uint32_t debugViewMode;
            uint32_t lightEnabled; // 旧 padding を転用
            // ここまでで HLSL 側 r0〜r3 に対応（lightColor/lightIntensity が r4）
            float lightColor[3];
            float lightIntensity;
            // 水面メッシュのワールドXZ範囲（RTWaterCaustics.hlsl の cbuffer 末尾と一致させること）。
            // regionValid == 0 なら範囲制限なし
            float regionCenterXZ[2];
            float regionHalfExtentXZ[2];
            uint32_t regionValid;
            // 波長依存の吸収係数 σa [1/m]（RGB）。水面描画の WaterFrameConstants と同値を同期
            float absorptionCoeff[3];
            Matrix4x4 invViewProj; // WorldPosition ターゲット廃止に伴う深度復元用
        };

        static constexpr Cb::Field kWaterCausticsConstantsFields[] = {
            CB_FIELD(WaterCausticsConstants, maxTraceDistance), CB_FIELD(WaterCausticsConstants, surfaceBias),
            CB_FIELD(WaterCausticsConstants, intensityScale), CB_FIELD(WaterCausticsConstants, padding0),
            CB_FIELD(WaterCausticsConstants, lightDirection), CB_FIELD(WaterCausticsConstants, screenWidth),
            CB_FIELD(WaterCausticsConstants, screenHeight), CB_FIELD(WaterCausticsConstants, fftOceanPad1),
            CB_FIELD(WaterCausticsConstants, fftOceanPad0), CB_FIELD(WaterCausticsConstants, fftOceanPad2),
            CB_FIELD(WaterCausticsConstants, refractiveIndex), CB_FIELD(WaterCausticsConstants, debugDisplayScale),
            CB_FIELD(WaterCausticsConstants, debugViewMode), CB_FIELD(WaterCausticsConstants, lightEnabled),
            CB_FIELD(WaterCausticsConstants, lightColor), CB_FIELD(WaterCausticsConstants, lightIntensity),
            CB_FIELD(WaterCausticsConstants, regionCenterXZ), CB_FIELD(WaterCausticsConstants, regionHalfExtentXZ),
            CB_FIELD(WaterCausticsConstants, regionValid), CB_FIELD(WaterCausticsConstants, absorptionCoeff),
            CB_FIELD(WaterCausticsConstants, invViewProj),
        };
        CB_VERIFY_LAYOUT(WaterCausticsConstants, kWaterCausticsConstantsFields);
        CB_BIND_HLSL(WaterCausticsConstants, kWaterCausticsConstantsFields, "WaterCausticsConstants");
    }

    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");
    static_assert(sizeof(WaterCausticsConstants) == 176,
        "WaterCausticsConstants size mismatch with HLSL cbuffer");
    // 個別フィールドの境界チェックはフィールド表（下）が全フィールド分やるので不要

    bool WaterCausticsRayTracingManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr)
    {
        // パイプラインの差分（シェーダー・エントリ名・SRV 名・定数サイズ）だけを記述する。
        // ルートシグネチャ構築〜シェーダーテーブルまでの手順は 3 マネージャ共通で
        // InitializeFromDesc（WaterRayTracingPassBase）が担う。
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterCausticsRayTracingManager";
        desc.outputDebugName = "RTWaterCaustics";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterCaustics.hlsl";
        desc.rayGenName = L"RTWaterCausticsRayGen";
        desc.missName = L"RTWaterCausticsMiss";
        desc.hitGroupName = L"RTWaterCausticsHitGroup";
        desc.closestHitName = L"RTWaterCausticsClosestHit";
        desc.outputUavName = "gCausticsOutput";
        static constexpr const char* kSrvTableNames[] = {
            "gSceneDepth", "gNormalRoughness", "gFFTOceanDisplacement", "gFFTOceanNormal" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterCausticsConstants";
        desc.constantsBytes = sizeof(WaterCausticsConstants);
        return InitializeFromDesc(dxCommon, descriptorAllocator, asMgr, desc);
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
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData =
            ResolveSurfaceDataForDispatch(surfaceData, resolvedSurfaceData);

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, width, height, dispatchSurfaceData, sceneDepthSRV, {});

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
        constants.regionCenterXZ[0] = dispatchSurfaceData.regionCenterXZ[0];
        constants.regionCenterXZ[1] = dispatchSurfaceData.regionCenterXZ[1];
        constants.regionHalfExtentXZ[0] = dispatchSurfaceData.regionHalfExtentXZ[0];
        constants.regionHalfExtentXZ[1] = dispatchSurfaceData.regionHalfExtentXZ[1];
        constants.regionValid = dispatchSurfaceData.regionValid;
        constants.absorptionCoeff[0] = settings_.absorptionCoeff[0];
        constants.absorptionCoeff[1] = settings_.absorptionCoeff[1];
        constants.absorptionCoeff[2] = settings_.absorptionCoeff[2];
        constants.invViewProj = invViewProj;

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

        UploadSurfaceDataForDispatch(dispatchSurfaceData, fftOceanInput);

        BindAndDispatchRays(
            cmdList,
            resources,
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
                dispatchSurfaceData.waterHeight,
                dispatchSurfaceData.simulationType,
                dispatchSurfaceData.activeWaveCount,
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

    ID3D12Resource* WaterCausticsRayTracingManager::GetCausticsResource(ViewID viewId) const
    {
        return GetOutputResourceBase(static_cast<uint32_t>(viewId));
    }

    D3D12_RESOURCE_STATES& WaterCausticsRayTracingManager::GetCausticsCurrentState(ViewID viewId)
    {
        return GetOutputCurrentStateBase(static_cast<uint32_t>(viewId));
    }
}
