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
            Matrix4x4 invViewProjection; // 深度復元用
            float cameraPosition[3];
            float surfaceBias;
            float maxRayDistance;
            float refractionEta;
            float screenWidth;
            float screenHeight;
            float maxRefractionOffsetPixels;
            float debugDisplayScale;
            uint32_t debugViewMode;
            // 水中の点を照らす値（DeferredLighting の水中ライティング・RT コースティクスと同じ値）
            float causticsIntensityScale;
            float underwaterAbsorption[3];
            uint32_t underwaterLightingEnabled;
        };

        static constexpr Cb::Field kWaterRefractionConstantsFields[] = {
            CB_FIELD(WaterRefractionConstants, viewProjection),
            CB_FIELD(WaterRefractionConstants, invViewProjection),
            CB_FIELD(WaterRefractionConstants, cameraPosition), CB_FIELD(WaterRefractionConstants, surfaceBias),
            CB_FIELD(WaterRefractionConstants, maxRayDistance),
            CB_FIELD(WaterRefractionConstants, refractionEta),
            CB_FIELD(WaterRefractionConstants, screenWidth), CB_FIELD(WaterRefractionConstants, screenHeight),
            CB_FIELD(WaterRefractionConstants, maxRefractionOffsetPixels),
            CB_FIELD(WaterRefractionConstants, debugDisplayScale),
            CB_FIELD(WaterRefractionConstants, debugViewMode),
            CB_FIELD(WaterRefractionConstants, causticsIntensityScale),
            CB_FIELD(WaterRefractionConstants, underwaterAbsorption),
            CB_FIELD(WaterRefractionConstants, underwaterLightingEnabled),
        };
        CB_VERIFY_LAYOUT(WaterRefractionConstants, kWaterRefractionConstantsFields);
        CB_BIND_HLSL(WaterRefractionConstants, kWaterRefractionConstantsFields, "WaterRefractionConstants");
    }

    static_assert(sizeof(WaterRefractionConstants) == 192,
        "WaterRefractionConstants size mismatch with HLSL cbuffer");
    static_assert(sizeof(WaterWaveParam) == 32,
        "WaterWaveParam size mismatch with HLSL wave struct");

    // 屈折固有の構成（シェーダーパス・バインド名・出力フォーマット）を基盤へ渡す
    bool WaterRefractionRayTracingManager::Initialize(const RTEffectInitContext& init)
    {
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterRefractionRayTracingManager";
        desc.outputDebugName = "RTWaterRefraction";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterRefraction.hlsl";
        desc.rayGenName = L"RTWaterRefractionRayGen";
        desc.outputUavName = "gRefractionOutput";
        static constexpr const char* kSrvTableNames[] = {
            "gSceneDepth", "gSceneColor", "gFFTOceanDisplacement", "gFFTOceanNormal" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterRefractionConstants";
        desc.constantsBytes = sizeof(WaterRefractionConstants);
        // RTRefractionPayload {hitT, hitFlag, instanceIndex, primitiveIndex, barycentrics}
        // 隠れた屈折先で当たった点を照らすときに表・テクスチャ・ライトをヒープから番号で引く
        desc.extraConstantBufferName = "RTHitShadingConstants";
        desc.directlyIndexedHeap = true;
        return InitializeFromDesc(init, desc);
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
        const RTHitShadingInput& hitShading,
        const WaterUnderwaterLightingInput& underwaterLighting,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, width, height, surfaceData, sceneDepthSRV, sceneColorSRV);

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
        constants.surfaceBias = settings_.surfaceBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        constants.refractionEta = 1.0f / (std::max)(settings_.waterRefractiveIndex, 1.0e-4f);
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.maxRefractionOffsetPixels = settings_.maxRefractionOffsetPixels;
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;
        for (int c = 0; c < 3; ++c) {
            constants.underwaterAbsorption[c] = underwaterLighting.absorptionCoeff[c];
        }
        constants.underwaterLightingEnabled = underwaterLighting.enabled ? 1u : 0u;
        constants.causticsIntensityScale = underwaterLighting.causticsIntensityScale;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;

        const SurfaceConstantsUpload surface = UploadSurfaceDataForDispatch(surfaceData, fftOceanInput);

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
                surfaceData.waterHeight,
                surfaceData.simulationType,
                surface.constants.activeWaveCount,
                surfaceData.time,
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
            surface.address,
            {
                { "gSceneDepth", sceneDepthSRV },
                { "gSceneColor", sceneColorSRV },
                { "gFFTOceanDisplacement", fftDisplacementSRV },
                { "gFFTOceanNormal", fftNormalSRV },
            },
            &constants,
            width,
            height,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            {},
            UploadRTHitShadingConstants(*dxCommon_, hitShading));
    }
}
