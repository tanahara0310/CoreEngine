#include "pch.h"
#include "SceneViewport.h"

#ifdef USE_IMGUI
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
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

        windowRenderer_->DrawWindow("Game", textureHandle,
            [this](const SceneViewportWindowResult& result) {
                if (!result.hasImage) {
                    return;
                }

                viewportPos_ = result.imageMin;
                viewportSize_ = result.imageSize;
                isViewportHovered_ = result.isImageHovered;

#if defined(_DEBUG)
                Gizmo::Prepare(viewportPos_, viewportSize_);
                ImGuizmo::SetDrawlist();

                if (gizmoController_) {
                    SceneViewportDrawContext context{};
                    context.viewportPos = viewportPos_;
                    context.viewportSize = viewportSize_;
                    context.isViewportHovered = isViewportHovered_;
                    context.currentCamera = currentCamera_;
                    context.currentCamera2D = currentCamera2D_;
                    context.currentGameCamera3D = currentGameCamera3D_;
                    context.objectSelector = objectSelector_.get();
                    context.inputQuery = inputQuery_;
                    gizmoController_->Draw(context);
                }

                if (onModelDropped_ && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_FILE")) {
                        const char* modelFileName = static_cast<const char*>(payload->Data);
                        onModelDropped_(std::string(modelFileName));
                    }
                    ImGui::EndDragDropTarget();
                }
#endif
            });
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
#endif // USE_IMGUI
