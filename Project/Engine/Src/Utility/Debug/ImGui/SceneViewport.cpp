#include "SceneViewport.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/PostEffectManager.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Gizmo.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Camera/ICamera.h"


namespace CoreEngine
{

    void SceneViewport::Initialize()
    {
        objectSelector_ = std::make_unique<ObjectSelector>();
        objectSelector_->Initialize();

        // Scene/Gameのビューポート描画責務を専用レンダラーへ分離する。
        windowRenderer_ = std::make_unique<SceneViewportWindowRenderer>();

        // 選択更新責務を専用コントローラーへ分離する。
        selectionController_ = std::make_unique<SceneViewportSelectionController>();

        // ギズモ関連の描画責務を専用コントローラーへ初期化委譲する。
        gizmoController_ = std::make_unique<SceneViewportGizmoController>();
        gizmoController_->Initialize();
    }

    void SceneViewport::DrawSceneViewport(DirectXCommon* dxCommon, Render* render, PostEffectManager* postEffectManager)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle{};

        if (render) {
            if (auto* sceneViewTarget = render->GetRenderTarget("SceneView")) {
                textureHandle = sceneViewTarget->GetSRVHandle();
            }
        }

        if (textureHandle.ptr == 0) {
            if (postEffectManager) {
                textureHandle = postEffectManager->GetFinalDisplayTextureHandle();
            } else {
                textureHandle = dxCommon->GetOffScreenSrvHandle();
            }
        }

        if (!windowRenderer_) {
            return;
        }

        // SceneViewportは各責務クラスを調停する管理役に徹する。
        windowRenderer_->DrawWindow("Scene", textureHandle,
            [this](const SceneViewportWindowResult& result) {
                if (!result.hasImage) {
                    return;
                }

                viewportPos_ = result.imageMin;
                viewportSize_ = result.imageSize;
                isViewportHovered_ = result.isImageHovered;

                // ギズモ準備（Image描画後に設定）
                Gizmo::Prepare(viewportPos_, viewportSize_);
                ImGuizmo::SetDrawlist();

                if (!gizmoController_) {
                    return;
                }

                SceneViewportDrawContext context{};
                context.viewportPos = viewportPos_;
                context.viewportSize = viewportSize_;
                context.isViewportHovered = isViewportHovered_;
                context.currentCamera = currentCamera_;
                context.currentCamera2D = currentCamera2D_;
                context.currentGameCamera3D = currentGameCamera3D_;
                context.objectSelector = objectSelector_.get();
                gizmoController_->Draw(context);
            });
    }

    void SceneViewport::DrawGameViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle{};

        if (postEffectManager) {
            textureHandle = postEffectManager->GetFinalDisplayTextureHandle();
        } else {
            textureHandle = dxCommon->GetOffScreenSrvHandle();
        }

        if (!windowRenderer_) {
            return;
        }

        // Gameビューはレイアウト描画のみを担当し、操作要素は持たない。
        windowRenderer_->DrawWindow("Game", textureHandle);
    }

    void SceneViewport::UpdateObjectSelection(GameObjectManager* gameObjectManager, const ICamera* camera)
    {
        if (!selectionController_) {
            return;
        }

        selectionController_->UpdateObjectSelection(
            objectSelector_.get(),
            gameObjectManager,
            camera,
            viewportPos_,
            viewportSize_,
            isViewportHovered_);
    }

    void SceneViewport::UpdateSpriteSelection(GameObjectManager* gameObjectManager, const ICamera* camera)
    {
        if (!selectionController_) {
            return;
        }

        selectionController_->UpdateSpriteSelection(
            objectSelector_.get(),
            gameObjectManager,
            camera,
            viewportPos_,
            viewportSize_,
            isViewportHovered_);
    }

}
