#include "CameraTransformEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>
#include <numbers>

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"

namespace CoreEngine
{
    namespace
    {
        constexpr float kRadToDeg = 180.0f / static_cast<float>(std::numbers::pi);
        constexpr float kDegToRad = static_cast<float>(std::numbers::pi) / 180.0f;
    }

    void CameraTransformEditorModule::Update(const CameraEditorContext& context)
    {
        // 初期版ではリアルタイム更新ロジックは持たず、描画操作のみを扱う。
        (void)context;
    }

    void CameraTransformEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        // 新API前提: 3Dアクティブカメラを明示取得して編集する。
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            ImGui::TextDisabled("アクティブな3Dカメラがありません。");
            return;
        }

        const std::string& activeName = context.cameraManager->GetActiveCameraName(CameraType::Camera3D);
        ImGui::Text("アクティブ3D: %s", activeName.c_str());
        ImGui::Separator();

        // ReleaseCamera: SRTを直接編集する。
        if (auto* releaseCamera = dynamic_cast<Camera*>(active3D)) {
            Vector3 translate = releaseCamera->GetTranslate();
            Vector3 rotate = releaseCamera->GetRotate();
            Vector3 scale = releaseCamera->GetScale();

            bool changed = false;
            changed |= ImGui::DragFloat3("位置", &translate.x, 0.1f);

            Vector3 rotateDeg = {
                rotate.x * kRadToDeg,
                rotate.y * kRadToDeg,
                rotate.z * kRadToDeg
            };
            if (ImGui::DragFloat3("回転 (度)", &rotateDeg.x, 0.5f, -360.0f, 360.0f)) {
                rotate = {
                    rotateDeg.x * kDegToRad,
                    rotateDeg.y * kDegToRad,
                    rotateDeg.z * kDegToRad
                };
                changed = true;
            }

            changed |= ImGui::DragFloat3("スケール", &scale.x, 0.01f, 0.01f, 10.0f);

            if (changed) {
                // 編集結果を一括反映し、次フレーム描画で反映できる状態にする。
                releaseCamera->SetTranslate(translate);
                releaseCamera->SetRotate(rotate);
                releaseCamera->SetScale(scale);
            }

            if (ImGui::Button("カメラをリセット")) {
                releaseCamera->Reset();
            }
            return;
        }

        // DebugCamera: Orbitパラメータを編集する。
        if (auto* debugCamera = dynamic_cast<DebugCamera*>(active3D)) {
            Vector3 target = debugCamera->GetTarget();
            float distance = debugCamera->GetDistance();
            float pitchDeg = debugCamera->GetPitch() * kRadToDeg;
            float yawDeg = debugCamera->GetYaw() * kRadToDeg;

            bool changed = false;
            changed |= ImGui::DragFloat3("注視点", &target.x, 0.1f);
            changed |= ImGui::DragFloat("距離", &distance, 0.1f, 0.1f, 10000.0f);
            changed |= ImGui::SliderFloat("ピッチ (度)", &pitchDeg, -89.0f, 89.0f, "%.1f");
            changed |= ImGui::SliderFloat("ヨー (度)", &yawDeg, -360.0f, 360.0f, "%.1f");

            if (changed) {
                debugCamera->SetTarget(target);
                debugCamera->SetDistance(distance);
                debugCamera->SetPitch(pitchDeg * kDegToRad);
                debugCamera->SetYaw(yawDeg * kDegToRad);
            }

            if (ImGui::Button("カメラをリセット")) {
                debugCamera->Reset();
            }
            return;
        }

        ImGui::TextDisabled("現在のカメラタイプはトランスフォーム編集に未対応です。");
    }
}

#endif // _DEBUG
