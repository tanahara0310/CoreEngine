#include "pch.h"
#include "CameraListEditorModule.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"

#include "Camera/CameraManager.h"

namespace CoreEngine
{
    void CameraListEditorModule::Update(const CameraEditorContext& context)
    {
        // 初期機能ではフレーム更新ロジックを持たない。
        (void)context;
    }

    void CameraListEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        auto* cameraManager = context.cameraManager;
        const auto& allCameras = cameraManager->GetAllCameras();

        // ===== どちらの視点で覗くか（Scene / Game）=====
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "ビュー");
        UI::Separator();

        bool useScene = cameraManager->IsUsingSceneCamera();
        if (ImGui::RadioButton("エディタ視点 (キー 1)", useScene)) {
            cameraManager->SetUseSceneCamera(true);
        }
        if (ImGui::RadioButton("ゲーム視点 (キー 2)", !useScene)) {
            cameraManager->SetUseSceneCamera(false);
        }
        ImGui::Text("描画中: %s", cameraManager->GetViewCameraName().c_str());
        UI::Hint("描画・ギズモ・ピッキングはすべてこのカメラを使います。");

        UI::Spacing();

        // ===== 役割の割り当て =====
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "3Dカメラの役割");
        UI::Separator();
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() != CameraType::Camera3D) {
                continue;
            }

            ImGui::PushID(name.c_str());
            ImGui::Text("%s", name.c_str());
            UI::SameLine();
            if (ImGui::SmallButton("エディタ視点にする")) {
                cameraManager->SetSceneCameraName(name);
            }
            UI::SameLine();
            if (ImGui::SmallButton("ゲーム視点にする")) {
                cameraManager->SetGameCameraName(name);
            }
            ImGui::PopID();
        }

        UI::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "2Dカメラ");
        UI::Separator();
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() != CameraType::Camera2D) {
                continue;
            }

            bool isActive = (context.cameraManager->GetActiveCameraName(CameraType::Camera2D) == name);
            if (ImGui::RadioButton(name.c_str(), isActive)) {
                context.cameraManager->SetActiveCamera(name, CameraType::Camera2D);
            }
        }
    }
}

#endif // _DEBUG
