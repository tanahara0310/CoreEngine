#include "pch.h"
#include "RayTracingSubsystem.h"
#include <algorithm>
#include <vector>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "GameObject/Model/ModelGameObject.h"
#include "GameObject/GameObjectManager.h"
#include "Camera/ICamera.h"
#include "Math/MathCore.h"
#include "Scene/SceneManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void RayTracingSubsystem::BuildAccelerationStructures(
        const RenderContext& context,
        DirectXCommon* dx,
        ModelManager* modelManager,
        SceneManager* sceneManager)
    {
        auto* asMgr = context.accelerationStructureManager;
        if (!asMgr || !asMgr->IsSupported() || !dx) {
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

        // DXR TLAS 構築（シーン内の全 ModelGameObject からインスタンスを収集）
        if (!sceneManager) {
            return;
        }

        auto* objMgr = sceneManager->GetCurrentGameObjectManager();
        if (!objMgr) {
            return;
        }

        std::vector<AccelerationStructureManager::InstanceDesc> tlasInstances;

        for (auto& obj : objMgr->GetAllObjects()) {
            if (!obj || !obj->IsActive()) continue;

            // ModelGameObject にダウンキャスト
            auto* modelObj = dynamic_cast<ModelGameObject*>(obj.get());
            if (!modelObj) continue;

            // 半透明オブジェクト（水面など）は RT シャドウのキャスターから除外する
            if (modelObj->GetBlendMode() != BlendMode::kBlendModeNone) continue;

            auto* model = modelObj->GetModel();
            if (!model) continue;

            auto* resource = model->GetModelResource();
            if (!resource || !resource->HasBLAS()) continue;

            AccelerationStructureManager::InstanceDesc inst;
            inst.blasIndex = resource->GetBLASIndex();
            inst.SetTransform(modelObj->GetTransform().GetWorldMatrix());
            tlasInstances.push_back(inst);
        }

        if (!tlasInstances.empty()) {
            asMgr->BuildTLAS(dx->GetCommandList(), tlasInstances);
        }
    }

    bool RayTracingSubsystem::BuildShadowStageContext(
        const RenderContext& context,
        DirectXCommon* dx,
        ID3D12GraphicsCommandList* cmdList,
        ShadowStageContext& outStageContext)
    {
        auto* rtShadow = context.rtShadowManager;
        if (!rtShadow || !rtShadow->IsInitialized()) return false;
        if (!context.gBufferManager || !context.lightManager || !context.sceneManager) return false;
        if (!dx || !cmdList) return false;

        // TODO(Stage 2d 積み残し): ここは実行中のビューに関わらず GameView のカメラを使っている。
        // ReflectionView へ RT シャドウを出すと行列が食い違うが、現在は
        // Scene::BuildRenderViewRequests() が反射ビューを発行しないため発火しない。
        // ViewID::ReflectionView を復活させる場合は、ビュー別のカメラ供給を先に用意すること。
        ICamera* camera = context.sceneManager->GetGameViewCamera3D();
        if (!camera) return false;

        outStageContext.rtShadow = rtShadow;

        // WorldPosition ターゲット廃止に伴い、深度から復元する（ビュー別に差し替わる FrameBlackboard 経由）
        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(
                FrameBlackboard::SceneDepth, outStageContext.sceneDepthSRV);
        }
        outStageContext.projection = camera->GetProjectionMatrix();
        outStageContext.invViewProj = MathCore::Matrix::Inverse(
            camera->GetViewMatrix() * outStageContext.projection);
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
            auto* dirLight = context.lightManager->GetDirectionalLight(li);
            if (!dirLight || !dirLight->enabled) continue;
            body(li, *dirLight);
        }
    }

    void RayTracingSubsystem::DispatchRTShadowTrace(
        const RenderContext& context,
        DirectXCommon* dx,
        ID3D12GraphicsCommandList* cmdList,
        RayTracingShadowManager::ViewID viewId)
    {
        ShadowStageContext stage;
        if (!BuildShadowStageContext(context, dx, cmdList, stage)) return;

        // 全ライト分 DispatchRays を先に実行（GPU パイプラインを詰めるため）
        ForEachShadowCastingLight(context, [&](uint32_t li, const Light& light) {
            stage.rtShadow->Dispatch(
                cmdList,
                stage.sceneDepthSRV,
                stage.normalSRV,
                stage.motionVectorSRV,
                light.direction,
                stage.invViewProj,
                stage.width,
                stage.height,
                viewId,
                li);
            });

        // GBuffer 入力の前後状態遷移は RenderGraph 側の自動遷移へ委譲する。
    }

    void RayTracingSubsystem::DispatchRTShadowTemporal(
        const RenderContext& context,
        DirectXCommon* dx,
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
                stage.width,
                stage.height,
                viewId,
                li);
            });
    }

    void RayTracingSubsystem::DispatchRTShadowDenoise(
        const RenderContext& context,
        DirectXCommon* dx,
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
                stage.width,
                stage.height,
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
        DirectXCommon* dx,
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

        outDispatchContext.camera = context.sceneManager->GetGameViewCamera3D();
        if (!outDispatchContext.camera) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "RayTracingSubsystem: {} dispatch skipped. camera is null.",
                debugLabel);
            return false;
        }

        // 深度は WorldPosition ターゲット廃止に伴いここから復元する
        // （ビュー別に差し替わるので FrameBlackboard 経由で取る）
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

        outDispatchContext.viewProjection =
            outDispatchContext.camera->GetViewMatrix() * outDispatchContext.camera->GetProjectionMatrix();
        outDispatchContext.cameraPosition = outDispatchContext.camera->GetPosition();
        outDispatchContext.width = static_cast<UINT>(dx->GetClientWidth());
        outDispatchContext.height = static_cast<UINT>(dx->GetClientHeight());

        // FFT Ocean 経路のときだけ波面テクスチャを接続する。
        // Gerstner 経路では enabled=0 のままにして、シェーダー側を解析評価へ倒す。
        if (context.fftOceanManager
            && context.fftOceanManager->IsInitialized()
            && surfaceData.simulationType == kWaterSurfaceModelTypeFFTOcean) {
            const FFTOceanManager::Settings& fftSettings = context.fftOceanManager->GetSettings();
            outDispatchContext.fftOceanInput.displacementSRV = context.fftOceanManager->GetDisplacementSRVHandle();
            outDispatchContext.fftOceanInput.normalSRV = context.fftOceanManager->GetNormalSRVHandle();
            outDispatchContext.fftOceanInput.resolution = fftSettings.resolution;
            outDispatchContext.fftOceanInput.patchLength = fftSettings.patchLength;
            outDispatchContext.fftOceanInput.enabled = 1;
        }

        return true;
    }

    void RayTracingSubsystem::DispatchWaterRefraction(
        const RenderContext& context,
        DirectXCommon* dx,
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

        rtWaterRefraction->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            dispatchContext.sceneColorSRV,
            dispatchContext.viewProjection,
            dispatchContext.cameraPosition,
            surfaceData,
            dispatchContext.fftOceanInput,
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterReflection(
        const RenderContext& context,
        DirectXCommon* dx,
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

        rtWaterReflection->Dispatch(
            cmdList,
            dispatchContext.sceneDepthSRV,
            dispatchContext.sceneColorSRV,
            dispatchContext.viewProjection,
            dispatchContext.cameraPosition,
            surfaceData,
            dispatchContext.fftOceanInput,
            dispatchContext.width,
            dispatchContext.height,
            viewId);
    }

    void RayTracingSubsystem::DispatchWaterCaustics(
        const RenderContext& context,
        DirectXCommon* dx,
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
            if (Light* mainLight = context.lightManager->GetDirectionalLight(0);
                mainLight && mainLight->enabled) {
                lightInput.direction = MathCore::Vector::Normalize(mainLight->direction);
                lightInput.color = { mainLight->color.x, mainLight->color.y, mainLight->color.z };
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
}
