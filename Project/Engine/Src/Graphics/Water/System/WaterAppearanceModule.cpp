#include "pch.h"
#include "WaterAppearanceModule.h"

#include "Camera/Camera.h"
#include "Camera/CameraStructs.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/SkyBox/SkyBoxComponent.h"
#include "Graphics/Water/Surface/WaterRenderResources.h"
#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Scene/Feature/ISceneFeature.h"

namespace CoreEngine
{
    void WaterAppearanceModule::FindSky(SceneContext& scene)
    {
        // 空気遠近感は「シーンに空（大気散乱の SkyBox）があるか」で決める（EnvironmentFeature が先に SkyBox を確定させている）
        if (!scene.gameObjectManager) {
            return;
        }
        if (auto* skyBox = scene.gameObjectManager->FindFirstComponent<SkyBoxComponent>()) {
            skyBox_.Set(skyBox);
        }
    }

    void WaterAppearanceModule::Reset()
    {
        skyBox_.Reset();
    }

    void WaterAppearanceModule::ApplySettings(const WaterFrameContext& frame) const
    {
        const WaterSurfaceSettings& surface = frame.settings.surface;
        WaterSurfaceComponent& waterPlane = frame.waterPlane;
        waterPlane.SetBaseColor(surface.baseColor);
        waterPlane.SetRoughness(surface.roughness);
        waterPlane.SetFresnelParameters(surface.fresnelScale, surface.fresnelF0);
        waterPlane.SetDepthFade(surface.depthFadeEnabled);
        waterPlane.SetWaterOpticalCoefficients(surface.absorption, surface.scattering);
        waterPlane.SetRefractiveIndex(surface.refractiveIndex);
    }

    void WaterAppearanceModule::Bind(const WaterFrameContext& frame, WaterFrameBinding& binding) const
    {
        // 水越しの背景色に使うシーンの色
        if (auto* render = frame.scene.engine ? frame.scene.engine->GetService<Render>() : nullptr) {
            if (auto* renderTargetManager = render->GetRenderTargetManager()) {
                RenderTarget* sceneColorTarget =
                    renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColorSnapshot);
                if (!sceneColorTarget) {
                    sceneColorTarget = renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColor);
                }
                if (sceneColorTarget) {
                    binding.resources.sceneColorSRV = sceneColorTarget->GetSRVHandle();
                }
            }
        }

        // 深さによる色の変化に使うシーンの深度
        if (auto* sceneDepth = frame.domain.GetSceneDepth()) {
            binding.resources.sceneDepthSRV = sceneDepth->GetDepthSRVHandle();
        }

        // 深度を距離へ直すのに使う描画カメラのクリップ距離（エディタと実行時で変わる）
        if (frame.scene.gameViewCamera3D) {
            const CameraParameters cameraParams = frame.scene.gameViewCamera3D->GetParameters();
            binding.cameraNearZ = cameraParams.nearClip;
            binding.cameraFarZ = cameraParams.farClip;
        }

        // 空気遠近感・空の環境光・空の映り込み
        if (auto* atmosphere = frame.domain.GetAtmosphereManager()) {
            const bool apEnabled = skyBox_.Get() != nullptr
                && atmosphere->IsConstantBufferReady()
                && atmosphere->AreLUTsReady();

            binding.resources.atmosphere = atmosphere;
            binding.resources.cameraVolumeSRV = atmosphere->GetCameraVolumeLUTSRVHandle();
            binding.resources.skyViewSRV = atmosphere->GetSkyViewLUTSRVHandle();
            binding.aerialPerspectiveEnabled = apEnabled;

            // 水中の散乱光の天空光（DeferredLighting の空の環境光と同じ SH・同じ倍率）
            binding.resources.skyIrradianceSRV = atmosphere->GetSkyIrradianceSHSRVHandle();
            binding.skyAmbientScale = atmosphere->GetSkyAmbientScale();
            binding.skyAmbientEnabled = apEnabled
                && atmosphere->IsSkyAmbientEnabled()
                && atmosphere->IsSkyAmbientReady();

            // 空と雲のキューブマップを映り込みへ重ねる
            binding.resources.skyEnvironmentSRV = atmosphere->GetSkySpecularSRVHandle();
            binding.skyEnvReflectionEnabled = apEnabled
                && atmosphere->IsSkySpecularEnabled()
                && atmosphere->IsSkyEnvironmentReady();
        }
    }
}
