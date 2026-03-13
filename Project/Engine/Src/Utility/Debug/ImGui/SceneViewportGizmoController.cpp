#include "SceneViewportGizmoController.h"

#include "ObjectSelector.h"
#include "Gizmo.h"
#include "Camera/ICamera.h"
#include "Camera/Release/Camera.h"
#include "Graphics/Texture/TextureManager.h"
#include "Math/MathCore.h"

#include <string>
#include <numbers>

namespace CoreEngine
{
    void SceneViewportGizmoController::Initialize()
    {
        // ギズモ描画に必要なアイコン群をまとめて読み込む。
        LoadGizmoIcons();
    }

    void SceneViewportGizmoController::Draw(const SceneViewportDrawContext& context)
    {
        if (!context.objectSelector) {
            return;
        }

        // 操作UI（ツールバー）を先に描画し、現在のギズモモードを確定する。
        DrawGizmoToolbar(context);

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

    void SceneViewportGizmoController::DrawGizmoToolbar(const SceneViewportDrawContext& context)
    {
        if (!context.objectSelector || !iconsLoaded_) {
            return;
        }

        struct ToolButton {
            const char* tooltip;
            Gizmo::Mode mode;
            ImGuiKey shortcutKey;
            D3D12_GPU_DESCRIPTOR_HANDLE iconHandle;
        };

        const ToolButton kButtons[] = {
            { "移動  [W]", Gizmo::Mode::Translate, ImGuiKey_W, gizmoTranslateIcon_ },
            { "回転  [E]", Gizmo::Mode::Rotate,    ImGuiKey_E, gizmoRotateIcon_ },
            { "拡縮  [R]", Gizmo::Mode::Scale,     ImGuiKey_R, gizmoScaleIcon_ },
        };
        static const int kButtonCount = static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0]));

        // ビューポートホバー中かつギズモ非操作中のみキーボードショートカットを受け付ける。
        if (context.isViewportHovered && !ImGuizmo::IsUsing()) {
            for (const auto& btn : kButtons) {
                if (ImGui::IsKeyPressed(btn.shortcutKey, false)) {
                    context.objectSelector->SetGizmoMode(btn.mode);
                }
            }
        }

        constexpr float kIconSize = 25.0f;
        constexpr float kPadding = 4.0f;
        constexpr float kSpacing = 3.0f;
        constexpr float kRounding = 6.0f;
        constexpr float kMargin = 8.0f;
        constexpr float kToggleHeight = 18.0f;

        const float buttonSize = kIconSize + kPadding * 2.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 toolbarOrigin = ImVec2(context.viewportPos.x + kMargin, context.viewportPos.y + kMargin);

        float bgWidth;
        float bgHeight;

        if (isToolbarCollapsed_) {
            bgWidth = buttonSize;
            bgHeight = kToggleHeight;
        } else {
            const float totalIconsHeight = buttonSize * kButtonCount + kSpacing * (kButtonCount - 1);
            bgWidth = buttonSize;
            bgHeight = kToggleHeight + kSpacing + totalIconsHeight;
        }

        drawList->AddRectFilled(
            toolbarOrigin,
            ImVec2(toolbarOrigin.x + bgWidth, toolbarOrigin.y + bgHeight),
            IM_COL32(20, 20, 20, 185),
            kRounding);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(kPadding, kPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, kSpacing));

        ImGui::SetCursorScreenPos(toolbarOrigin);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.10f, 1.00f));

        const ImVec2 uvMin = isToolbarCollapsed_ ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 uvMax = isToolbarCollapsed_ ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
        if (ImGui::ImageButton("##GizmoToggle", (ImTextureID)gizmoToggleIcon_.ptr,
            ImVec2(kIconSize, kToggleHeight - kPadding * 2.0f), uvMin, uvMax)) {
            isToolbarCollapsed_ = !isToolbarCollapsed_;
        }

        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip(isToolbarCollapsed_ ? "ツールバーを展開" : "ツールバーを折り畳む");
        }

        if (!isToolbarCollapsed_) {
            const Gizmo::Mode currentMode = context.objectSelector->GetGizmoMode();

            for (int i = 0; i < kButtonCount; i++) {
                const auto& btn = kButtons[i];
                const bool isActive = (currentMode == btn.mode);

                ImGui::SetCursorScreenPos(ImVec2(
                    toolbarOrigin.x,
                    toolbarOrigin.y + kToggleHeight + kSpacing + i * (buttonSize + kSpacing)));

                if (isActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 1.00f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.49f, 0.88f, 1.00f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.18f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.32f, 0.32f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.12f, 0.12f, 1.00f));
                }

                ImGui::PushID(i);
                if (ImGui::ImageButton(("##GizmoIcon" + std::to_string(i)).c_str(), (ImTextureID)btn.iconHandle.ptr, ImVec2(kIconSize, kIconSize))) {
                    context.objectSelector->SetGizmoMode(btn.mode);
                }
                ImGui::PopID();

                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", btn.tooltip);
                }
            }
        }

        ImGui::PopStyleVar(3);
    }

    void SceneViewportGizmoController::LoadGizmoIcons()
    {
        auto& texManager = TextureManager::GetInstance();

        if (!texManager.IsInitialized()) {
            return;
        }

        try {
            auto translateTex = texManager.Load("gizumoTransform.png");
            auto rotateTex = texManager.Load("gizumoRotate.png");
            auto scaleTex = texManager.Load("gizumoScale.png");
            auto toggleTex = texManager.Load("bottomArrow.png");
            auto cameraTex = texManager.Load("Engine/Assets/Textures/Icon/camera.png");

            gizmoTranslateIcon_ = translateTex.gpuHandle;
            gizmoRotateIcon_ = rotateTex.gpuHandle;
            gizmoScaleIcon_ = scaleTex.gpuHandle;
            gizmoToggleIcon_ = toggleTex.gpuHandle;
            gameCameraIcon_ = cameraTex.gpuHandle;

            iconsLoaded_ = true;
            gameCameraIconLoaded_ = (gameCameraIcon_.ptr != 0);
        }
        catch (...) {
            iconsLoaded_ = false;
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
