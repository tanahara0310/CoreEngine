#include "CameraAnimationEditor.h"

#ifdef _DEBUG

#include <imgui.h>
#include <algorithm>

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#include "Math/Easing/EasingUtil.h"

namespace CoreEngine
{
    void CameraAnimationEditor::DrawUI(CameraPresetManager& presetManager, CameraManager* cameraManager)
    {
        ImGui::TextColored(ImVec4(0.8f, 0.3f, 1.0f, 1.0f), "カメラアニメーション (JSON)");
        ImGui::Separator();

        const auto& presetFiles = presetManager.GetCurrentPresetList();
        if (presetFiles.size() < 2) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "アニメーションには2つ以上のプリセットが必要です");
            ImGui::Separator();
            ImGui::Text("Presetsタブでプリセットを保存してください。");
            return;
        }

        animFromFileIndex_ = std::clamp(animFromFileIndex_, 0, static_cast<int>(presetFiles.size()) - 1);
        animToFileIndex_ = std::clamp(animToFileIndex_, 0, static_cast<int>(presetFiles.size()) - 1);

        if (animation_.isAnimating) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[再生中]");
            ImGui::ProgressBar(animation_.progress, ImVec2(-1, 0));
            ImGui::Text("%.1f / %.1f秒", animation_.progress * animation_.duration, animation_.duration);

            if (ImGui::Button("停止", ImVec2(-1, 0))) {
                animation_.isAnimating = false;
            }
            return;
        }

        ImGui::Text("開始プリセット:");
        if (ImGui::BeginCombo("##FromFile", presetFiles[animFromFileIndex_].c_str())) {
            for (int i = 0; i < static_cast<int>(presetFiles.size()); i++) {
                bool isSelected = (i == animFromFileIndex_);
                if (ImGui::Selectable(presetFiles[i].c_str(), isSelected)) {
                    animFromFileIndex_ = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Text("終了プリセット:");
        if (ImGui::BeginCombo("##ToFile", presetFiles[animToFileIndex_].c_str())) {
            for (int i = 0; i < static_cast<int>(presetFiles.size()); i++) {
                bool isSelected = (i == animToFileIndex_);
                if (ImGui::Selectable(presetFiles[i].c_str(), isSelected)) {
                    animToFileIndex_ = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::DragFloat("時間 (秒)", &animDuration_, 0.1f, 0.1f, 10.0f, "%.1f");

        const char* easingTypes[] = { "Linear", "EaseInOut", "EaseIn", "EaseOut" };
        ImGui::Combo("イージング", &easingTypeIndex_, easingTypes, IM_ARRAYSIZE(easingTypes));

        ImGui::Separator();
        if (ImGui::Button("アニメーション開始", ImVec2(-1, 0))) {
            StartAnimation(presetManager, cameraManager, animFromFileIndex_, animToFileIndex_);
        }
    }

    void CameraAnimationEditor::StartAnimation(CameraPresetManager& presetManager, CameraManager* cameraManager, int fromIndex, int toIndex)
    {
        if (!cameraManager) {
            return;
        }

        const auto& presetFiles = presetManager.GetCurrentPresetList();
        if (fromIndex < 0 || fromIndex >= static_cast<int>(presetFiles.size())) {
            return;
        }
        if (toIndex < 0 || toIndex >= static_cast<int>(presetFiles.size())) {
            return;
        }

        CameraSnapshot fromSnapshot;
        CameraSnapshot toSnapshot;
        const std::string fromPath = presetManager.BuildPresetFilePath(presetFiles[fromIndex]);
        const std::string toPath = presetManager.BuildPresetFilePath(presetFiles[toIndex]);

        if (!presetManager.LoadSnapshotFromFile(fromPath, fromSnapshot)
            || !presetManager.LoadSnapshotFromFile(toPath, toSnapshot)) {
            return;
        }

        animation_.isAnimating = true;
        animation_.progress = 0.0f;
        animation_.duration = animDuration_;
        animation_.fromSnapshot = fromSnapshot;
        animation_.toSnapshot = toSnapshot;
    }

    void CameraAnimationEditor::Update(CameraManager* cameraManager)
    {
        if (!animation_.isAnimating || !cameraManager) {
            return;
        }

        const float safeDuration = (animation_.duration <= 0.0f) ? 0.01f : animation_.duration;
        const float deltaTime = ImGui::GetIO().DeltaTime;
        animation_.progress += deltaTime / safeDuration;

        if (animation_.progress >= 1.0f) {
            animation_.progress = 1.0f;
            animation_.isAnimating = false;
        }

        float t = animation_.progress;
        switch (easingTypeIndex_) {
        case 1:
            t = EasingUtil::Apply(t, EasingUtil::Type::EaseInOutQuad);
            break;
        case 2:
            t = EasingUtil::Apply(t, EasingUtil::Type::EaseInQuad);
            break;
        case 3:
            t = EasingUtil::Apply(t, EasingUtil::Type::EaseOutQuad);
            break;
        default:
            break;
        }

        CameraSnapshot interpolated;
        const auto& from = animation_.fromSnapshot;
        const auto& to = animation_.toSnapshot;

        interpolated.isDebugCamera = from.isDebugCamera;
        if (from.isDebugCamera) {
            interpolated.target = EasingUtil::LerpVector3(from.target, to.target, t, EasingUtil::Type::Linear);
            interpolated.distance = from.distance + (to.distance - from.distance) * t;
            interpolated.pitch = from.pitch + (to.pitch - from.pitch) * t;
            interpolated.yaw = from.yaw + (to.yaw - from.yaw) * t;
        } else {
            interpolated.position = EasingUtil::LerpVector3(from.position, to.position, t, EasingUtil::Type::Linear);
            interpolated.rotation = EasingUtil::LerpVector3(from.rotation, to.rotation, t, EasingUtil::Type::Linear);
            interpolated.scale = EasingUtil::LerpVector3(from.scale, to.scale, t, EasingUtil::Type::Linear);
        }

        interpolated.parameters.fov = from.parameters.fov + (to.parameters.fov - from.parameters.fov) * t;
        interpolated.parameters.nearClip = from.parameters.nearClip + (to.parameters.nearClip - from.parameters.nearClip) * t;
        interpolated.parameters.farClip = from.parameters.farClip + (to.parameters.farClip - from.parameters.farClip) * t;
        interpolated.parameters.aspectRatio = from.parameters.aspectRatio + (to.parameters.aspectRatio - from.parameters.aspectRatio) * t;

        ICamera* activeCamera = cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!activeCamera) {
            return;
        }

        if (interpolated.isDebugCamera) {
            if (auto* debugCam = dynamic_cast<DebugCamera*>(activeCamera)) {
                debugCam->RestoreSnapshot(interpolated);
            }
        } else {
            if (auto* releaseCam = dynamic_cast<Camera*>(activeCamera)) {
                releaseCam->RestoreSnapshot(interpolated);
            }
        }
    }
}

#endif // _DEBUG
