#include "CameraActivationEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>

#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"

namespace CoreEngine
{
    namespace
    {
        const char* ToTypeLabel(CameraType type)
        {
            return (type == CameraType::Camera3D) ? "3D" : "2D";
        }
    }

    void CameraActivationEditorModule::Update(const CameraEditorContext& context)
    {
        // 初期版では描画操作のみを扱う。
        (void)context;
    }

    void CameraActivationEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        const auto& allCameras = context.cameraManager->GetAllCameras();
        if (allCameras.empty()) {
            ImGui::TextDisabled("登録カメラがありません。");
            return;
        }

        // 将来の検索/フィルタ拡張を見据えてテーブル形式で整理する。
        if (ImGui::BeginTable("CameraActivationTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupColumn("名前");
            ImGui::TableSetupColumn("種別", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("有効", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("アクティブ", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            for (const auto& [name, camera] : allCameras) {
                if (!camera) {
                    continue;
                }

                const CameraType type = camera->GetCameraType();
                const bool isCurrentTypeActive = (context.cameraManager->GetActiveCameraName(type) == name);

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ToTypeLabel(type));

                ImGui::TableSetColumnIndex(2);
                bool enabled = camera->GetActive();
                ImGui::PushID((name + "_enabled").c_str());
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    camera->SetActive(enabled);
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(3);
                ImGui::PushID((name + "_active").c_str());
                if (isCurrentTypeActive) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "現在");
                } else if (ImGui::SmallButton("設定")) {
                    context.cameraManager->SetActiveCamera(name, type);
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        const auto* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (active3D && !active3D->GetActive()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "警告: 現在の3Dカメラは無効で、更新が停止する可能性があります。");
        }
    }
}

#endif // _DEBUG
