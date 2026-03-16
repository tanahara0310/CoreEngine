#include "CameraListEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>

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

        // 3D/2Dごとに一覧を分けることで、将来の機能拡張（検索・タグ付け）に備える。
        const auto& allCameras = context.cameraManager->GetAllCameras();

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "3Dカメラ");
        ImGui::Separator();
        for (const auto& [name, camera] : allCameras) {
            if (camera->GetCameraType() != CameraType::Camera3D) {
                continue;
            }

            bool isActive = (context.cameraManager->GetActiveCameraName(CameraType::Camera3D) == name);
            if (ImGui::RadioButton(name.c_str(), isActive)) {
                context.cameraManager->SetActiveCamera(name, CameraType::Camera3D);
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "2Dカメラ");
        ImGui::Separator();
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
