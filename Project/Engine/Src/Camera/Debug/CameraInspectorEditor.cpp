#include "CameraInspectorEditor.h"

#ifdef _DEBUG

#include <imgui.h>
#include <numbers>

#include "Camera/ICamera.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"

namespace CoreEngine
{
    void CameraInspectorEditor::DrawCameraBasicInfo(ICamera* camera)
    {
        if (!camera) {
            return;
        }

        bool isActive = camera->GetActive();
        if (ImGui::Checkbox("有効", &isActive)) {
            camera->SetActive(isActive);
        }

        Vector3 pos = camera->GetPosition();
        ImGui::Text("位置: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
    }

    void CameraInspectorEditor::DrawDebugCameraInspector(DebugCamera* debugCamera)
    {
        if (!debugCamera) {
            return;
        }

        ImGui::PushID("DebugCamera");

        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "デバッグカメラ制御");
        ImGui::Separator();

        if (debugCamera->IsControlling()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[操作中]");
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "待機中");
        }

        ImGui::Separator();

        Vector3 target = debugCamera->GetTarget();
        if (ImGui::DragFloat3("注視点", &target.x, 0.1f)) {
            debugCamera->SetTarget(target);
        }

        float distance = debugCamera->GetDistance();
        if (ImGui::SliderFloat("距離", &distance, 0.1f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            debugCamera->SetDistance(distance);
        }

        float pitchDeg = debugCamera->GetPitch() * 180.0f / std::numbers::pi_v<float>;
        float yawDeg = debugCamera->GetYaw() * 180.0f / std::numbers::pi_v<float>;

        if (ImGui::SliderFloat("ピッチ", &pitchDeg, -89.0f, 89.0f, "%.1f°")) {
            debugCamera->SetPitch(pitchDeg * std::numbers::pi_v<float> / 180.0f);
        }

        if (ImGui::SliderFloat("ヨー", &yawDeg, 0.0f, 360.0f, "%.1f°")) {
            debugCamera->SetYaw(yawDeg * std::numbers::pi_v<float> / 180.0f);
        }

        ImGui::Separator();

        ImGui::Text("クイックビュー:");
        if (ImGui::Button("正面")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Front);
        ImGui::SameLine();
        if (ImGui::Button("背面")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Back);
        ImGui::SameLine();
        if (ImGui::Button("左")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Left);
        ImGui::SameLine();
        if (ImGui::Button("右")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Right);

        if (ImGui::Button("上")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Top);
        ImGui::SameLine();
        if (ImGui::Button("下")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Bottom);
        ImGui::SameLine();
        if (ImGui::Button("斜め")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::Diagonal);
        ImGui::SameLine();
        if (ImGui::Button("クローズアップ")) debugCamera->ApplyPreset(DebugCamera::CameraPreset::CloseUp);

        ImGui::Separator();

        if (ImGui::CollapsingHeader("カメラ設定", ImGuiTreeNodeFlags_None)) {
            auto settings = debugCamera->GetSettings();
            bool changed = false;

            changed |= ImGui::SliderFloat("回転感度", &settings.rotationSensitivity, 0.001f, 0.01f, "%.4f");
            changed |= ImGui::SliderFloat("パン感度", &settings.panSensitivity, 0.0001f, 0.001f, "%.4f");
            changed |= ImGui::SliderFloat("ズーム感度", &settings.zoomSensitivity, 0.1f, 5.0f, "%.2f");

            changed |= ImGui::Checkbox("スムーズ移動", &settings.smoothMovement);
            if (settings.smoothMovement) {
                ImGui::Indent();
                changed |= ImGui::SliderFloat("スムージング係数", &settings.smoothingFactor, 0.01f, 0.5f, "%.3f");
                ImGui::Unindent();
            }

            changed |= ImGui::Checkbox("Y軸反転", &settings.invertY);

            if (changed) {
                debugCamera->SetSettings(settings);
            }
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("カメラパラメータ", ImGuiTreeNodeFlags_None)) {
            auto params = debugCamera->GetParameters();
            bool paramsChanged = false;

            float fovDeg = params.GetFovDegrees();
            if (ImGui::SliderFloat("視野角 (FOV)", &fovDeg, 30.0f, 120.0f, "%.1f°")) {
                params.SetFovDegrees(fovDeg);
                paramsChanged = true;
            }

            paramsChanged |= ImGui::DragFloat("Near Clip", &params.nearClip, 0.01f, 0.01f, 10.0f, "%.2f");
            paramsChanged |= ImGui::DragFloat("Far Clip", &params.farClip, 1.0f, 10.0f, 10000.0f, "%.1f");

            paramsChanged |= ImGui::DragFloat("アスペクト比", &params.aspectRatio, 0.01f, 0.0f, 3.0f, "%.3f");
            ImGui::SameLine();
            if (ImGui::Button("パラメータをリセット", ImVec2(-1, 0))) {
                debugCamera->ResetParameters();
            }

            if (paramsChanged) {
                debugCamera->SetParameters(params);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("カメラをリセット", ImVec2(-1, 0))) {
            debugCamera->Reset();
        }

        ImGui::PopID();
    }

    void CameraInspectorEditor::DrawReleaseCameraInspector(Camera* camera)
    {
        if (!camera) {
            return;
        }

        ImGui::PushID("ReleaseCamera");

        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "リリースカメラ制御");
        ImGui::Separator();

        ImGui::Checkbox("LookAtモード", &releaseCameraLookAtMode_);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("指定した位置を注視するモード");
        }

        if (releaseCameraLookAtMode_) {
            ImGui::Indent();
            if (ImGui::DragFloat3("注視点", &releaseCameraLookAtTarget_.x, 0.1f)) {
                camera->LookAt(releaseCameraLookAtTarget_);
            }
            ImGui::Unindent();
        }

        ImGui::Separator();
        ImGui::Text("トランスフォーム:");

        Vector3 scale = camera->GetScale();
        Vector3 rotate = camera->GetRotate();
        Vector3 translate = camera->GetTranslate();

        if (ImGui::DragFloat3("スケール", &scale.x, 0.01f, 0.01f, 10.0f)) {
            camera->SetScale(scale);
        }

        Vector3 rotateDeg = {
            rotate.x * 180.0f / std::numbers::pi_v<float>,
            rotate.y * 180.0f / std::numbers::pi_v<float>,
            rotate.z * 180.0f / std::numbers::pi_v<float>
        };

        if (ImGui::DragFloat3("回転 (度)", &rotateDeg.x, 0.5f, -360.0f, 360.0f, "%.1f°")) {
            rotate = {
                rotateDeg.x * std::numbers::pi_v<float> / 180.0f,
                rotateDeg.y * std::numbers::pi_v<float> / 180.0f,
                rotateDeg.z * std::numbers::pi_v<float> / 180.0f
            };
            camera->SetRotate(rotate);
        }

        if (ImGui::DragFloat3("位置", &translate.x, 0.1f)) {
            camera->SetTranslate(translate);
        }

        ImGui::Separator();
        DrawCameraVectorInfo(camera);

        ImGui::Separator();
        if (ImGui::CollapsingHeader("カメラパラメータ", ImGuiTreeNodeFlags_None)) {
            auto params = camera->GetParameters();
            bool paramsChanged = false;

            float fovDeg = params.GetFovDegrees();
            if (ImGui::SliderFloat("視野角 (FOV)", &fovDeg, 30.0f, 120.0f, "%.1f°")) {
                params.SetFovDegrees(fovDeg);
                paramsChanged = true;
            }

            paramsChanged |= ImGui::DragFloat("Near Clip", &params.nearClip, 0.01f, 0.01f, 10.0f, "%.2f");
            paramsChanged |= ImGui::DragFloat("Far Clip", &params.farClip, 1.0f, 10.0f, 10000.0f, "%.1f");

            paramsChanged |= ImGui::DragFloat("アスペクト比", &params.aspectRatio, 0.01f, 0.0f, 3.0f, "%.3f");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0.0で自動計算");
            }

            if (ImGui::Button("パラメータをリセット", ImVec2(-1, 0))) {
                camera->ResetParameters();
            }

            if (paramsChanged) {
                camera->SetParameters(params);
            }
        }

        ImGui::Separator();
        if (showAdvancedSettings_) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "詳細情報:");
            ImGui::Text("回転(rad): (%.3f, %.3f, %.3f)", rotate.x, rotate.y, rotate.z);
            ImGui::Separator();
        }

        ImGui::Checkbox("詳細設定を表示", &showAdvancedSettings_);

        ImGui::Separator();
        if (ImGui::Button("カメラをリセット", ImVec2(-1, 0))) {
            camera->Reset();
            releaseCameraLookAtMode_ = false;
            releaseCameraLookAtTarget_ = Vector3{ 0.0f, 0.0f, 0.0f };
        }

        ImGui::PopID();
    }

    void CameraInspectorEditor::DrawCameraVectorInfo(Camera* camera)
    {
        if (!camera) {
            return;
        }

        if (ImGui::CollapsingHeader("方向ベクトル情報", ImGuiTreeNodeFlags_None)) {
            Vector3 forward = camera->GetForward();
            Vector3 right = camera->GetRight();
            Vector3 up = camera->GetUp();

            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "前方:");
            ImGui::SameLine();
            ImGui::Text("(%.3f, %.3f, %.3f)", forward.x, forward.y, forward.z);

            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "右:");
            ImGui::SameLine();
            ImGui::Text("(%.3f, %.3f, %.3f)", right.x, right.y, right.z);

            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "上:");
            ImGui::SameLine();
            ImGui::Text("(%.3f, %.3f, %.3f)", up.x, up.y, up.z);
        }
    }
}

#endif // _DEBUG
