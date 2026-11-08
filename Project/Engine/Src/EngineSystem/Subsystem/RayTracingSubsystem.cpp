#include "pch.h"
#include "RayTracingSubsystem.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RTScene.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Camera/View/ViewInfo.h"
#include "Scene/SceneManager.h"

namespace CoreEngine
{
    void RayTracingSubsystem::BuildAccelerationStructures(
        const RenderContext& context,
        GraphicsCore* dx,
        SceneManager* sceneManager)
    {
        auto* asMgr = context.accelerationStructureManager;
        auto* rtScene = context.rtScene;
        if (!asMgr || !asMgr->IsSupported() || !rtScene || !dx) {
            return;
        }

        // フレーム開始時に効果の前のフレームの状態を片付ける
        if (context.rtEffects) {
            context.rtEffects->BeginFrame();
        }

        // シーンの物を集め、使うモデルの BLAS を作って TLAS を組む
        // （シーンが無いフレームも空の TLAS を組み、前のシーンの TLAS とヒットの表を使わせない）
        RTScene::FrameInput input;
        input.objects = sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;
        input.cmdList = dx->GetCommandList();
        input.frame = context.frameNumber;
        input.hasCamera = context.frameViews && context.frameViews->GameView().isValid;
        if (input.hasCamera) {
            input.cameraPosition = context.frameViews->GameView().position;
        }
        input.deformer = context.vertexAnimationDeformer;
        // 凍結中は、同じシーンのままなら前の TLAS と材質の表を撃ち続ける
        if (RTDebug::IsSceneFrozen() && rtScene->IsSameSceneAsLastBuild(input)) {
            return;
        }
        rtScene->Build(input, *dx, *asMgr);
    }
}
