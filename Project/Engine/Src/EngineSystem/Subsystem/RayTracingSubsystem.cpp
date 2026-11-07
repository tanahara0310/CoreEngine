#include "pch.h"
#include "RayTracingSubsystem.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/RenderingTechnique/Lighting/DeferredLightingTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Camera/View/ViewInfo.h"
#include "Math/MathCore.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void RayTracingSubsystem::BuildAccelerationStructures(
        const RenderContext& context,
        GraphicsCore* dx,
        ModelManager* modelManager,
        SceneManager* sceneManager)
    {
        auto* asMgr = context.accelerationStructureManager;
        auto* rtScene = context.rtScene;
        if (!asMgr || !asMgr->IsSupported() || !rtScene || !dx) {
            return;
        }

        // フレーム開始時に RT シャドウの状態をリセット
        if (context.rtShadowManager) {
            context.rtShadowManager->ResetFrameState();
        }

        // BLAS 遅延ビルド（未構築のモデルリソース全てを対象）
        if (modelManager) {
            auto* cmdListForBLAS = dx->GetCommandList();
            modelManager->ForEachResource([&](ModelResource* resource) {
                asMgr->BuildBLASFromModelResource(cmdListForBLAS, resource);
                });
        }

        // シーンの物を集めて TLAS を組む（シーンが無いフレームも空の TLAS を組み、前のシーンの TLAS とヒットの表を使わせない）
        RTScene::FrameInput input;
        input.objects = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;
        input.cmdList = dx->GetCommandList();
        input.frame = context.frameNumber;
        input.hasCamera = context.frameViews && context.frameViews->GameView().isValid;
        if (input.hasCamera) {
            input.cameraPosition = context.frameViews->GameView().position;
        }
        input.deformer = context.vertexAnimationDeformer;
        rtScene->Build(input, *dx, *asMgr);
    }

    bool RayTracingSubsystem::BuildShadowStageContext(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        ShadowStageContext& outStageContext)
    {
        auto* rtShadow = context.rtShadowManager;
        if (!rtShadow || !rtShadow->IsInitialized()) return false;
        if (!context.gBufferManager || !context.lightManager || !context.sceneManager) return false;
        if (!dx || !cmdList) return false;

        // 実行中のビューの ViewInfo を使う
        if (!context.frameViews) return false;
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
        if (!view.isValid) return false;

        outStageContext.rtShadow = rtShadow;

        // WorldPosition ターゲット廃止に伴い、深度から復元する（FrameBlackboard 経由）
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneDepth, outStageContext.sceneDepthSRV);
        }
        outStageContext.projection = view.projection;
        outStageContext.invViewProj = view.invViewProjection;
        outStageContext.normalSRV =
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness);
        outStageContext.motionVectorSRV =
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::MotionVector);
        outStageContext.width = static_cast<UINT>(dx->GetClientWidth());
        outStageContext.height = static_cast<UINT>(dx->GetClientHeight());
        return true;
    }

    void RayTracingSubsystem::ForEachShadowCastingLight(
        const RenderContext& context,
        const std::function<void(uint32_t lightIndex, const Light& light)>& body)
    {
        const uint32_t maxLights = RayTracingShadowManager::kMaxDirectionalLights;
        for (uint32_t li = 0; li < LightManager::MAX_DIRECTIONAL_LIGHTS && li < maxLights; ++li) {
            const Light* dirLight = context.lightManager->GetDirectionalLight(li);
            if (!dirLight || !dirLight->enabled) continue;
            body(li, *dirLight);
        }
    }

    void RayTracingSubsystem::DispatchRTShadowTrace(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId,
        const RayTracingShadowWaterSurface& waterSurface)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 DispatchRays を先に実行（GPU パイプラインを詰めるため）
        // 水中の直接光を RT コースティクスが置き換えるのはメインライト（0 番）だけなので、
        // 水面を渡すのも 0 番だけ
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light& light) {
            stage.rtShadow->Dispatch(
                cmdList,
                stage.sceneDepthSRV,
                stage.normalSRV,
                light.direction,
                stage.invViewProj,
                stage.width,
                stage.height,
                viewId,
                li,
                &light,
                (li == 0) ? waterSurface : RayTracingShadowWaterSurface{});
            });

        // GBuffer 入力の前後状態遷移は RenderGraph 側の自動遷移へ委譲する。
    }

    void RayTracingSubsystem::DispatchRTShadowTemporal(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 テンポラル蓄積パス（空間前処理+再投影+Variance Clamping）
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light&) {
            stage.rtShadow->ApplyTemporal(
                cmdList,
                stage.normalSRV,
                stage.sceneDepthSRV,
                stage.motionVectorSRV,
                stage.projection,
                viewId,
                li);
            });
    }

    void RayTracingSubsystem::DispatchRTShadowDenoise(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 A-Trous デノイズをまとめて実行
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light&) {
            stage.rtShadow->Denoise(
                cmdList,
                stage.normalSRV,
                stage.sceneDepthSRV,
                stage.projection,
                viewId,
                li);
            });

        // RT シャドウの全ステージが終わったので、デバッグパネルが中間バッファ
        // （生マスク・履歴）を ImGui で表示できる状態へ整える。
        // パネルが要求していないフレームは何もしない。
        stage.rtShadow->PrepareDebugViews(cmdList);
    }

    bool RayTracingSubsystem::BuildWaterDispatchContext(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        const WaterSurfaceData& surfaceData,
        const char* debugLabel,
        bool requireSceneColor,
        WaterDispatchContext& outDispatchContext)
    {
        if (!context.gBufferManager || !context.sceneManager || !dx || !cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: {} dispatch skipped. gBuffer={} sceneManager={} dx={} cmdList={}",
                debugLabel,
                context.gBufferManager != nullptr,
                context.sceneManager != nullptr,
                dx != nullptr,
                cmdList != nullptr);
            return false;
        }

        if (!context.frameViews || !context.frameViews->Get(context.viewSettings.viewType).isValid) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: {} dispatch skipped. view is invalid.",
                debugLabel);
            return false;
        }
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);

        // 深度は WorldPosition ターゲット廃止に伴いここから復元する
        // （FrameBlackboard 経由で取る）
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneDepth, outDispatchContext.sceneDepthSRV);

            // カラーソースは水面合成前の SceneColor スナップショット
            // （空・雲・ゴッドレイ・島すべてを含み、水面のみ未合成）。
            if (!context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneColorSnapshot, outDispatchContext.sceneColorSRV)) {
                context.frameBlackboard->TryGetSrvHandle(
                    FrameBlackboard::SceneColor, outDispatchContext.sceneColorSRV);
            }
        }

        if (requireSceneColor && outDispatchContext.sceneColorSRV.ptr == 0) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "RayTracingSubsystem: {} dispatch skipped. SceneColorSnapshot/SceneColor SRV is invalid.",
                debugLabel);
            return false;
        }

        outDispatchContext.viewProjection = view.viewProjection;
        outDispatchContext.cameraPosition = view.position;
        outDispatchContext.width = static_cast<UINT>(dx->GetClientWidth());
        outDispatchContext.height = static_cast<UINT>(dx->GetClientHeight());

        // FFT の海で描いているときだけ波面テクスチャを接続する（それ以外は enabled = 0）
        outDispatchContext.fftOceanInput = surfaceData.fftOcean;

        return true;
    }

    void RayTracingSubsystem::DispatchWaterRefraction(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterRefractionRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterRefraction = context.rtWaterRefractionManager;
        if (!rtWaterRefraction || !rtWaterRefraction->IsInitialized()) {
            return;
        }

        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water refraction", true, dispatchContext)) {
            return;
        }

        // 水中の点の照らし方は、このフレームの DeferredLighting が水中の点に使った値にそろえる
        WaterUnderwaterLightingInput underwaterLighting{};
        if (context.renderingTechniqueManager && context.rtWaterCausticsManager) {
            if (auto* deferredLighting = context.renderingTechniqueManager->GetTechnique<DeferredLightingTechnique>(
                    RenderingTechniqueNames::DeferredLighting)) {
                const DeferredLightingTechnique::WaterCausticsDebugSettings& waterLighting =
                    deferredLighting->GetWaterCausticsDebugSettings();
                underwaterLighting.enabled = (waterLighting.waterVolumeEnabled != 0);
                for (int c = 0; c < 3; ++c) {
                    underwaterLighting.absorptionCoeff[c] = waterLighting.absorptionCoeff[c];
                }
                underwaterLighting.causticsIntensityScale =
                    context.rtWaterCausticsManager->GetSettings().intensityScale;
            }
        }

        rtWaterRefraction->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            dispatchContext.sceneColorSRV,
            dispatchContext.viewProjection,
            dispatchContext.cameraPosition,
            surfaceData,
            dispatchContext.fftOceanInput,
            BuildWaterHitShadingInput(context, dx),
            underwaterLighting,
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterReflection(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterReflectionRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterReflection = context.rtWaterReflectionManager;
        if (!rtWaterReflection || !rtWaterReflection->IsInitialized()) {
            return;
        }
        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water reflection", true, dispatchContext)) {
            return;
        }

        // 空キューブマップ（空＋雲）を反射パスへ渡す。
        // レイが空へ抜けたときの色を「実際にトレースした向き」でこのパス内で
        // 解決するために必要（Water.PS 側で平面法線の空と混ぜると二重像になる）。
        D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV{};
        if (auto* atmosphere = context.atmosphereManager) {
            if (atmosphere->IsSkySpecularEnabled() && atmosphere->IsSkyEnvironmentReady()) {
                skyEnvironmentSRV = atmosphere->GetSkySpecularSRVHandle();
            }
        }

        // 水面の影はメインライト（0 番）で調べる（Water.PS が掛けるのも 0 番の項だけ）
        WaterSunShadowInput sunShadow{};
        if (context.lightManager) {
            if (const Light* mainLight = context.lightManager->GetDirectionalLight(0);
                mainLight && mainLight->enabled) {
                sunShadow.direction = CoreEngine::Normalize(mainLight->direction);
                sunShadow.enabled = true;
            }
        }

        rtWaterReflection->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            dispatchContext.sceneColorSRV,
            dispatchContext.viewProjection,
            dispatchContext.cameraPosition,
            surfaceData,
            dispatchContext.fftOceanInput,
            skyEnvironmentSRV,
            sunShadow,
            BuildWaterHitShadingInput(context, dx),
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    WaterHitShadingInput RayTracingSubsystem::BuildWaterHitShadingInput(
        const RenderContext& context, GraphicsCore* dx)
    {
        WaterHitShadingInput input{};
        const DescriptorAllocator* descriptors = dx ? dx->GetDescriptorAllocator() : nullptr;
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

    void RayTracingSubsystem::DispatchWaterCaustics(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterCausticsRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterCaustics = context.rtWaterCausticsManager;
        if (!rtWaterCaustics || !rtWaterCaustics->IsInitialized()) {
            return;
        }
        // コースティクスは屈折・反射と違い SceneColor へ再投影しないので必須ではない
        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water caustics", false, dispatchContext)) {
            return;
        }

        // シーンの実際のディレクショナルライトの方向・色・強度・有効フラグを RT コースティクスへ
        // 伝える。以前は方向しか渡しておらず、ライトを消してもコースティクスが消えない、
        // ライトの色/強度を変えても常に一定の白い光量のままになる不具合の原因だった。
        WaterCausticsRayTracingManager::LightInput lightInput{};
        if (context.lightManager) {
            if (const Light* mainLight = context.lightManager->GetDirectionalLight(0);
                mainLight && mainLight->enabled) {
                lightInput.direction = CoreEngine::Normalize(mainLight->direction);
                // 大気透過率適用済みの実効色（DeferredLighting・SS版コースティクスと同一基準）。
                // 生の color を渡すと日没時に置換前後で輝度が食い違い水面線が不連続になる
                const Vector3 effectiveColor =
                    context.lightManager->GetEffectiveLightColorRGB(*mainLight);
                lightInput.color = { effectiveColor.x, effectiveColor.y, effectiveColor.z };
                // オーサリングは照度 [lx]。シェーダーが期待する従来単位へ換算する
                lightInput.intensity = LightUnits::LuxToShader(mainLight->intensity);
                lightInput.enabled = true;
            } else {
                lightInput.enabled = false;
            }
        } else {
            lightInput.enabled = false;
        }

        rtWaterCaustics->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            context.gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness),
            lightInput,
            surfaceData,
            dispatchContext.fftOceanInput,
            MathCore::Matrix::Inverse(dispatchContext.viewProjection),
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterSeabed(
        const RenderContext& context,
        GraphicsCore* dx,
        ID3D12GraphicsCommandList* cmdList,
        WaterSeabedRayTracingManager::ViewID viewId,
        const WaterSurfaceData& surfaceData)
    {
        auto* rtWaterSeabed = context.rtWaterSeabedManager;
        if (!rtWaterSeabed || !rtWaterSeabed->IsInitialized()) {
            return;
        }
        WaterDispatchContext dispatchContext;
        if (!BuildWaterDispatchContext(
            context, dx, cmdList, surfaceData, "water seabed", false, dispatchContext)) {
            return;
        }

        // Gerstner 経路では変位テクスチャを読まないが、宣言されたスロットには何かを差す
        WaterRayTracingPassBase::FFTOceanInput fftOceanInput = dispatchContext.fftOceanInput;
        if (fftOceanInput.displacementSRV.ptr == 0) {
            fftOceanInput.displacementSRV = dispatchContext.sceneDepthSRV;
        }

        rtWaterSeabed->Dispatch(cmdList, surfaceData, fftOceanInput, viewId);
    }
}
