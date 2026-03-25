#include "SceneViewportGizmoController.h"

#include "ObjectSelector.h"
#include "Gizmo.h"
#include "Camera/ICamera.h"
#include "Camera/Release/Camera.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"

#include <numbers>

namespace CoreEngine
{
    void SceneViewportGizmoController::Initialize()
    {
        // Sceneビューで使用するアイコンを読み込む。
        LoadGizmoIcons();
    }

    void SceneViewportGizmoController::Draw(const SceneViewportDrawContext& context)
    {
        if (!context.objectSelector) {
            return;
        }

        if (context.isViewportHovered && !ImGuizmo::IsUsing()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                context.objectSelector->SetGizmoMode(Gizmo::Mode::Translate);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                context.objectSelector->SetGizmoMode(Gizmo::Mode::Rotate);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                context.objectSelector->SetGizmoMode(Gizmo::Mode::Scale);
            }
        }

        // 次にGameカメラのアイコン描画と選択判定を行う。
        DrawGameCameraOverlay(context);

        // 最後に実際のギズモ操作を描画する。
        const bool hasSelected3D = (context.currentCamera && context.objectSelector->GetSelectedObject());
        const bool hasSelected2D = (context.currentCamera2D && context.objectSelector->GetSelectedSprite());

        if (hasSelected3D) {
            context.objectSelector->DrawGizmo(context.currentCamera);
        } else if (hasSelected2D) {
            context.objectSelector->DrawGizmo2D(context.currentCamera2D);
        } else {
            DrawGameCameraGizmo(context);
        }
    }

    void SceneViewportGizmoController::LoadGizmoIcons()
    {
        auto& texManager = TextureManager::GetInstance();

        if (!texManager.IsInitialized()) {
            return;
        }

        try {
            auto cameraTex = texManager.Load("Engine/Assets/Textures/Icon/camera.png");

            gameCameraIcon_ = cameraTex.gpuHandle;

            gameCameraIconLoaded_ = (gameCameraIcon_.ptr != 0);
        }
        catch (...) {
            gameCameraIconLoaded_ = false;
        }
    }

    void SceneViewportGizmoController::DrawGameCameraOverlay(const SceneViewportDrawContext& context)
    {
        if (!context.currentCamera || !context.currentGameCamera3D || !gameCameraIconLoaded_) {
            isGameCameraSelected_ = false;
            return;
        }

        Matrix4x4 viewProj = MathCore::Matrix::Multiply(
            context.currentCamera->GetViewMatrix(),
            context.currentCamera->GetProjectionMatrix());
        Vector3 ndcPos = MathCore::CoordinateTransform::TransformCoord(
            context.currentGameCamera3D->GetPosition(),
            viewProj);

        if (ndcPos.z < 0.0f || ndcPos.z > 1.0f) {
            return;
        }
        if (ndcPos.x < -1.2f || ndcPos.x > 1.2f || ndcPos.y < -1.2f || ndcPos.y > 1.2f) {
            return;
        }

        const float screenX = context.viewportPos.x + (ndcPos.x * 0.5f + 0.5f) * context.viewportSize.x;
        const float screenY = context.viewportPos.y + (-ndcPos.y * 0.5f + 0.5f) * context.viewportSize.y;

        const float iconSize = 24.0f;
        const ImVec2 minPos(screenX - iconSize * 0.5f, screenY - iconSize * 0.5f);
        const ImVec2 maxPos(screenX + iconSize * 0.5f, screenY + iconSize * 0.5f);
        const bool iconHovered = ImGui::IsMouseHoveringRect(minPos, maxPos, false);

        if (context.isViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (iconHovered) {
                isGameCameraSelected_ = true;
                if (context.objectSelector) {
                    context.objectSelector->ClearSelection();
                }
            } else if (!ImGuizmo::IsUsing() && !Gizmo::IsOver()) {
                isGameCameraSelected_ = false;
            }
        }

        const ImU32 tintColor = isGameCameraSelected_ ? IM_COL32(255, 220, 128, 255) : IM_COL32(255, 255, 255, 255);

        ImGui::GetWindowDrawList()->AddImage(
            (ImTextureID)gameCameraIcon_.ptr,
            minPos,
            maxPos,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            tintColor);
    }

    void SceneViewportGizmoController::DrawGameCameraGizmo(const SceneViewportDrawContext& context)
    {
        if (!isGameCameraSelected_ || !context.currentCamera || !context.currentGameCamera3D || !context.objectSelector) {
            return;
        }

        auto* gameCamera = dynamic_cast<Camera*>(context.currentGameCamera3D);
        if (!gameCamera) {
            return;
        }

        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
        switch (context.objectSelector->GetGizmoMode()) {
        case Gizmo::Mode::Translate:
            operation = ImGuizmo::TRANSLATE;
            break;
        case Gizmo::Mode::Rotate:
            operation = ImGuizmo::ROTATE;
            break;
        case Gizmo::Mode::Scale:
            operation = ImGuizmo::SCALE;
            break;
        default:
            operation = ImGuizmo::TRANSLATE;
            break;
        }

        Matrix4x4 viewMatrix = context.currentCamera->GetViewMatrix();
        Matrix4x4 projectionMatrix = context.currentCamera->GetProjectionMatrix();
        Matrix4x4 worldMatrix = MathCore::Matrix::MakeAffine(
            gameCamera->GetScale(),
            gameCamera->GetRotate(),
            gameCamera->GetTranslate());

        ImGuizmo::SetOrthographic(false);
        const bool changed = ImGuizmo::Manipulate(
            &viewMatrix.m[0][0],
            &projectionMatrix.m[0][0],
            operation,
            ImGuizmo::LOCAL,
            &worldMatrix.m[0][0]);

        if (!changed) {
            return;
        }

        constexpr float kDegToRad = static_cast<float>(std::numbers::pi) / 180.0f;
        Vector3 translation{};
        Vector3 rotationDegrees{};
        Vector3 scale{};
        ImGuizmo::DecomposeMatrixToComponents(
            &worldMatrix.m[0][0],
            &translation.x,
            &rotationDegrees.x,
            &scale.x);

        gameCamera->SetTranslate(translation);
        gameCamera->SetRotate(Vector3(
            rotationDegrees.x * kDegToRad,
            rotationDegrees.y * kDegToRad,
            rotationDegrees.z * kDegToRad));
        gameCamera->SetScale(scale);
        gameCamera->Update();
    }
}
