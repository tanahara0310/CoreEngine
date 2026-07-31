#include "pch.h"
#include "CameraTransformEditorModule.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include <numbers>

#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#include "Camera/Camera.h"

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
        Camera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!active3D) {
            UI::Hint("アクティブな3Dカメラがありません。");
            return;
        }

        const std::string& activeName = context.cameraManager->GetActiveCameraName(CameraType::Camera3D);
        ImGui::Text("アクティブ3D: %s", activeName.c_str());
        UI::Separator();

        // 軌道コントローラが付いているカメラは、位置ではなく「注視点まわりの軌道」を編集する。
        // （コントローラが毎フレーム Transform を上書きするため、位置を直接いじっても戻される）
        if (auto* orbit = context.cameraManager->GetActiveOrbitController()) {
            auto state = orbit->GetState();
            float pitchDeg = state.pitch * kRadToDeg;
            float yawDeg = state.yaw * kRadToDeg;

            bool changed = false;
            changed |= UI::DragVec3("注視点", state.target, 0.1f);
            changed |= UI::DragFloat("距離", state.distance, 0.1f, 0.1f, 10000.0f);
            changed |= UI::SliderFloat("ピッチ (度)", pitchDeg, -89.0f, 89.0f, "%.1f");
            changed |= UI::SliderFloat("ヨー (度)", yawDeg, -360.0f, 360.0f, "%.1f");

            if (changed) {
                state.pitch = pitchDeg * kDegToRad;
                state.yaw = yawDeg * kDegToRad;
                orbit->SetState(state);
            }

            if (ImGui::Button("カメラをリセット")) {
                orbit->Reset();
            }
            return;
        }

        // コントローラ無しのカメラは SRT を直接編集する。
        Vector3 translate = active3D->GetTranslate();
        Vector3 rotate = active3D->GetRotate();
        Vector3 scale = active3D->GetScale();

        bool changed = false;
        changed |= UI::DragVec3("位置", translate, 0.1f);

        Vector3 rotateDeg = {
            rotate.x * kRadToDeg,
            rotate.y * kRadToDeg,
            rotate.z * kRadToDeg
        };
        if (UI::DragVec3("回転 (度)", rotateDeg, 0.5f, -360.0f, 360.0f)) {
            rotate = {
                rotateDeg.x * kDegToRad,
                rotateDeg.y * kDegToRad,
                rotateDeg.z * kDegToRad
            };
            changed = true;
        }

        changed |= UI::DragVec3("スケール", scale, 0.01f, 0.01f, 10.0f);

        if (changed) {
            active3D->SetTranslate(translate);
            active3D->SetRotate(rotate);
            active3D->SetScale(scale);
        }

        if (ImGui::Button("カメラをリセット")) {
            active3D->Reset();
        }
    }
}

#endif // _DEBUG
