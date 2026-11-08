#include "pch.h"
#include "RTHitShading.h"

#include <algorithm>
#include <cstring>

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    namespace {
        /// @brief ヒットシェーディングの定数（Include/RayTracing/RTHitShading.hlsli の RTHitShadingConstants）
        struct RTHitShadingConstants {
            uint32_t hitInstanceTableIndex;
            uint32_t hitSubMeshTableIndex;
            uint32_t directionalLightsIndex;
            uint32_t directionalLightCount;
            uint32_t skyIrradianceSHIndex;
            uint32_t skyAmbientEnabled;
            float skyAmbientScale;
            uint32_t hitShadingEnabled;
            uint32_t skySpecularMapIndex;
            uint32_t skySpecularEnabled;
            uint32_t cloudShadowMapIndex;
            float cloudShadowStrength;
            float cloudShadowRegionCenterXZ[2];
            float cloudShadowRegionSize;
            float cloudShadowAnchorY;
            float cloudShadowEdgeFadeStart;
            uint32_t wetSurfaceEnabled;
            float wetSurfaceBottomHeight;
            float wetSurfaceSamplesPerMeter;
            float wetSurfaceRegion[4];
            Vector4 wetSurfaceTable[kWaterWetnessSampleCount / 4];
        };
        static constexpr Cb::Field kRTHitShadingConstantsFields[] = {
            CB_FIELD(RTHitShadingConstants, hitInstanceTableIndex),
            CB_FIELD(RTHitShadingConstants, hitSubMeshTableIndex),
            CB_FIELD(RTHitShadingConstants, directionalLightsIndex),
            CB_FIELD(RTHitShadingConstants, directionalLightCount),
            CB_FIELD(RTHitShadingConstants, skyIrradianceSHIndex),
            CB_FIELD(RTHitShadingConstants, skyAmbientEnabled),
            CB_FIELD(RTHitShadingConstants, skyAmbientScale),
            CB_FIELD(RTHitShadingConstants, hitShadingEnabled),
            CB_FIELD(RTHitShadingConstants, skySpecularMapIndex),
            CB_FIELD(RTHitShadingConstants, skySpecularEnabled),
            CB_FIELD(RTHitShadingConstants, cloudShadowMapIndex),
            CB_FIELD(RTHitShadingConstants, cloudShadowStrength),
            CB_FIELD(RTHitShadingConstants, cloudShadowRegionCenterXZ),
            CB_FIELD(RTHitShadingConstants, cloudShadowRegionSize),
            CB_FIELD(RTHitShadingConstants, cloudShadowAnchorY),
            CB_FIELD(RTHitShadingConstants, cloudShadowEdgeFadeStart),
            CB_FIELD(RTHitShadingConstants, wetSurfaceEnabled),
            CB_FIELD(RTHitShadingConstants, wetSurfaceBottomHeight),
            CB_FIELD(RTHitShadingConstants, wetSurfaceSamplesPerMeter),
            CB_FIELD(RTHitShadingConstants, wetSurfaceRegion),
            CB_FIELD(RTHitShadingConstants, wetSurfaceTable),
        };
        CB_VERIFY_LAYOUT(RTHitShadingConstants, kRTHitShadingConstantsFields);
        CB_BIND_HLSL(RTHitShadingConstants, kRTHitShadingConstantsFields, "RTHitShadingConstants");
    }

    RTHitShadingInput BuildRTHitShadingInput(const RenderContext& context)
    {
        RTHitShadingInput input{};
        const DescriptorAllocator* descriptors = context.dxCommon ? context.dxCommon->GetDescriptorAllocator() : nullptr;
        const RTScene* rtScene = context.rtScene;
        if (!descriptors || !rtScene || !rtScene->HasHitShadingTables()) {
            return input;
        }
        input.instanceTableIndex = rtScene->GetHitInstanceTableIndex();
        input.subMeshTableIndex = rtScene->GetHitSubMeshTableIndex();
        input.enabled = (input.instanceTableIndex != UINT32_MAX && input.subMeshTableIndex != UINT32_MAX);

        // DeferredLighting と同じライトの配列（0 番がメインライト）
        if (context.lightManager) {
            input.directionalLightsIndex =
                descriptors->GetSRVHeapIndex(context.lightManager->GetDirectionalLightsSRVHandle());
            if (input.directionalLightsIndex != UINT32_MAX) {
                input.directionalLightCount = (std::min)(
                    context.lightManager->GetLightCount(LightType::Directional),
                    LightManager::GetMaxLightCount(LightType::Directional));
            }
        }

        // 空の光（DeferredLighting と同じ有効条件）
        if (auto* atmosphere = context.atmosphereManager) {
            const bool skyAmbientUsable = atmosphere->IsAtmosphereActive()
                && atmosphere->IsSkyAmbientEnabled()
                && atmosphere->IsSkyAmbientReady()
                && atmosphere->GetSkyIrradianceSHSRVHandle().ptr != 0;
            if (skyAmbientUsable) {
                input.skyIrradianceSHIndex = descriptors->GetSRVHeapIndex(atmosphere->GetSkyIrradianceSHSRVHandle());
                input.skyAmbientEnabled = (input.skyIrradianceSHIndex != UINT32_MAX);
                input.skyAmbientScale = atmosphere->GetSkyAmbientScale();

                const bool skySpecularUsable = atmosphere->IsSkySpecularEnabled()
                    && atmosphere->IsSkyEnvironmentReady()
                    && atmosphere->GetSkySpecularSRVHandle().ptr != 0;
                if (skySpecularUsable) {
                    input.skySpecularMapIndex = descriptors->GetSRVHeapIndex(atmosphere->GetSkySpecularSRVHandle());
                    input.skySpecularEnabled = (input.skySpecularMapIndex != UINT32_MAX);
                }
            }
        }

        // 雲の影（DeferredLighting と同じマップと範囲）
        if (auto* clouds = context.volumetricCloudManager; clouds && clouds->AreCloudsActive() && context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE cloudShadowMap{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::CloudShadowMap, cloudShadowMap)) {
                const CloudShadowShaderConstants& cloudShadow = clouds->GetCloudShadowConstants();
                input.cloudShadowMapIndex = descriptors->GetSRVHeapIndex(cloudShadowMap);
                input.cloudShadowStrength = cloudShadow.sceneStrength;
                input.cloudShadowRegionCenterXZ[0] = cloudShadow.regionCenterX;
                input.cloudShadowRegionCenterXZ[1] = cloudShadow.regionCenterZ;
                input.cloudShadowRegionSize = cloudShadow.regionSizeM;
                input.cloudShadowAnchorY = cloudShadow.anchorWorldY;
                input.cloudShadowEdgeFadeStart = cloudShadow.edgeFadeStart;
            }
        }

        // 濡れ跡（DeferredLighting と同じ表と範囲）
        if (const WaterSurfaceData* surface = context.waterSurfaceState;
            surface && surface->regionValid != 0 && surface->wetness.enabled != 0) {
            input.wetSurface = surface->wetness;
            input.wetSurfaceRegionCenterXZ[0] = surface->regionCenterXZ[0];
            input.wetSurfaceRegionCenterXZ[1] = surface->regionCenterXZ[1];
            input.wetSurfaceRegionHalfExtentXZ[0] = surface->regionHalfExtentXZ[0];
            input.wetSurfaceRegionHalfExtentXZ[1] = surface->regionHalfExtentXZ[1];
        }
        return input;
    }

    D3D12_GPU_VIRTUAL_ADDRESS UploadRTHitShadingConstants(GraphicsCore& dx, const RTHitShadingInput& input)
    {
        RTHitShadingConstants constants{};
        constants.hitInstanceTableIndex = input.instanceTableIndex;
        constants.hitSubMeshTableIndex = input.subMeshTableIndex;
        constants.directionalLightsIndex = input.directionalLightsIndex;
        constants.directionalLightCount = input.directionalLightCount;
        constants.skyIrradianceSHIndex = input.skyIrradianceSHIndex;
        constants.skyAmbientEnabled = input.skyAmbientEnabled ? 1u : 0u;
        constants.skyAmbientScale = input.skyAmbientScale;
        constants.hitShadingEnabled = input.enabled ? 1u : 0u;
        constants.skySpecularMapIndex = input.skySpecularMapIndex;
        constants.skySpecularEnabled = input.skySpecularEnabled ? 1u : 0u;
        constants.cloudShadowMapIndex = input.cloudShadowMapIndex;
        constants.cloudShadowStrength = input.cloudShadowStrength;
        constants.cloudShadowRegionCenterXZ[0] = input.cloudShadowRegionCenterXZ[0];
        constants.cloudShadowRegionCenterXZ[1] = input.cloudShadowRegionCenterXZ[1];
        constants.cloudShadowRegionSize = input.cloudShadowRegionSize;
        constants.cloudShadowAnchorY = input.cloudShadowAnchorY;
        constants.cloudShadowEdgeFadeStart = input.cloudShadowEdgeFadeStart;
        constants.wetSurfaceEnabled = input.wetSurface.enabled;
        constants.wetSurfaceBottomHeight = input.wetSurface.bottomHeight;
        constants.wetSurfaceSamplesPerMeter = input.wetSurface.samplesPerMeter;
        constants.wetSurfaceRegion[0] = input.wetSurfaceRegionCenterXZ[0];
        constants.wetSurfaceRegion[1] = input.wetSurfaceRegionCenterXZ[1];
        constants.wetSurfaceRegion[2] = input.wetSurfaceRegionHalfExtentXZ[0];
        constants.wetSurfaceRegion[3] = input.wetSurfaceRegionHalfExtentXZ[1];
        static_assert(sizeof(constants.wetSurfaceTable) == sizeof(input.wetSurface.wetness),
            "濡れ跡の表の標本数を C++ と HLSL でそろえる");
        std::memcpy(constants.wetSurfaceTable, input.wetSurface.wetness, sizeof(constants.wetSurfaceTable));
        return dx.GetUploadRing().AllocateConstants(constants);
    }
}
