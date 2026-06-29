#include "pch.h"
#include "WaterTestScene.h"

#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Utility/FrameRate/FrameRateController.h"

#include "../../../../../../Application/Src/Sample/SampleScene/WaterTestScene/WaterSurfaceRuntimeController.cpp"
#include "../../../../../../Application/Src/Sample/SampleScene/WaterTestScene/WaterSurfaceParameterPanel.cpp"
#include "../../../../../../Application/Src/Sample/SampleScene/WaterTestScene/WaterSurfaceDebugPanel.cpp"
#include "../../../../../../Application/Src/Sample/SampleScene/WaterTestScene/WaterSceneController.cpp"

using namespace CoreEngine;

void WaterTestScene::OnInitialize() {
    SetSceneName("WaterTestScene");
    waterController_.Initialize(*this, *engine_);
}

void WaterTestScene::OnUpdate() {
    auto* frameRate = engine_->GetComponent<FrameRateController>();
    const float deltaTime = frameRate ? frameRate->GetDeltaTime() : (1.0f / 60.0f);
    waterController_.Update(*engine_, deltaTime);
}

void WaterTestScene::Draw() {
    BaseScene::Draw();
}

std::vector<RenderViewRequest> WaterTestScene::BuildRenderViewRequests()
{
    std::vector<RenderViewRequest> requests;

    WaterPlaneObject* waterPlane = waterController_.GetWaterPlane();
    if (!waterPlane) {
        return requests;
    }

    ICamera* mainCamera = GetGameViewCamera3D();
    const float planeHeight = waterController_.GetWaterHeight();

    RenderViewRequest request{};
    request.isEnabled = true;
    request.name = "WaterReflection";
    request.viewSettings.viewType = RenderViewType::ReflectionView;
    request.viewSettings.enableSSAO = false;
    request.viewSettings.enableRTShadow = false;
    request.viewSettings.enablePostEffect = false;
    request.viewSettings.enableBackBuffer = false;
    request.viewSettings.sceneColorTargetName = RenderTargetNames::ReflectionView;
    request.beforeExecute = [this, mainCamera, planeHeight]() {
        SetupWaterReflectionView(mainCamera, planeHeight);
    };
    request.afterExecute = [this, mainCamera]() {
        RestoreWaterReflectionView(mainCamera);
    };
    request.completionCallback = [this](const RenderViewResult& result) {
        ApplyWaterRenderViewResult(result);
    };

    requests.push_back(std::move(request));
    return requests;
}

void WaterTestScene::SetupWaterReflectionView(ICamera* mainCamera, float planeHeight)
{
    reflectionPass_.SetupReflectionCamera(mainCamera, planeHeight);

    if (WaterPlaneObject* waterPlane = waterController_.GetWaterPlane()) {
        waterPlane->SetClipPlane(reflectionPass_.GetClipPlane(), true);
        waterPlane->UpdateFrameConstants();
    }
}

void WaterTestScene::RestoreWaterReflectionView(ICamera* mainCamera)
{
    reflectionPass_.RestoreMainCamera(mainCamera);
}

void WaterTestScene::ApplyWaterRenderViewResult(const RenderViewResult& result)
{
    WaterPlaneObject* waterPlane = waterController_.GetWaterPlane();
    if (!waterPlane) {
        return;
    }

    waterController_.ApplyWaterRenderViewResult(result);
    waterPlane->SetClipPlane(reflectionPass_.GetClipPlane(), false);
    waterPlane->UpdateFrameConstants();
}

const WaterSurfaceData* WaterTestScene::GetWaterRefractionSurfaceData() const
{
    return waterController_.GetWaterRefractionSurfaceData();
}

void WaterTestScene::Finalize() {
    BaseScene::Finalize();
}
