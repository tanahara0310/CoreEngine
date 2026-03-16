#include "CameraFollowEditorModule.h"

#ifdef _DEBUG

#include <imgui.h>
#include <algorithm>

#include "Camera/CameraManager.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"
#include "ObjectCommon/GameObject.h"
#include "ObjectCommon/GameObjectManager.h"

namespace CoreEngine
{
    void CameraFollowEditorModule::Update(const CameraEditorContext& context)
    {
        // 機能OFFまたは依存不足時は追従/注視更新を行わない。
        if (!enabled_ || !context.cameraManager || !context.gameObjectManager) {
            return;
        }

        // 設定された対象オブジェクトを名前で探索する。
        GameObject* targetObject = FindTargetObject(context);
        if (!targetObject) {
            return;
        }

        const Vector3 targetPosition = targetObject->GetWorldPosition();

        // アクティブ3Dカメラの型に合わせて追従/注視を適用する。
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (dynamic_cast<Camera*>(active3D)) {
            ApplyToReleaseCamera(context, targetPosition);
        } else if (dynamic_cast<DebugCamera*>(active3D)) {
            ApplyToDebugCamera(context, targetPosition);
        }
    }

    void CameraFollowEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        ImGui::Checkbox("追従/注視を有効", &enabled_);
        ImGui::Checkbox("位置を追従", &followEnabled_);
        ImGui::SameLine();
        ImGui::Checkbox("対象を注視", &lookAtEnabled_);

        ImGui::Separator();

        if (!context.gameObjectManager) {
            ImGui::TextDisabled("GameObjectManagerが未設定のため追従対象を選択できません。");
            return;
        }

        // 対象候補リストを毎フレーム構築し、最新のシーン状態を反映する。
        const auto& objects = context.gameObjectManager->GetAllObjects();
        if (objects.empty()) {
            ImGui::TextDisabled("シーン内に追従対象オブジェクトがありません。");
            return;
        }

        if (ImGui::BeginCombo("追従対象オブジェクト", targetObjectName_.empty() ? "未選択" : targetObjectName_.c_str())) {
            for (const auto& obj : objects) {
                if (!obj) {
                    continue;
                }

                const std::string& name = obj->GetName();
                const bool selected = (targetObjectName_ == name);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    targetObjectName_ = name;
                    statusMessage_.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::DragFloat3("追従オフセット", &followOffset_.x, 0.05f);
        ImGui::DragFloat3("注視オフセット", &lookAtOffset_.x, 0.05f);
        ImGui::SliderFloat("追従スムージング", &followSmoothing_, 0.01f, 1.0f, "%.2f");

        if (followSmoothing_ < 0.01f) {
            followSmoothing_ = 0.01f;
        }

        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (dynamic_cast<Camera*>(active3D)) {
            ImGui::TextDisabled("現在の3Dカメラ: ReleaseCamera (追従+注視対応)");
        } else if (dynamic_cast<DebugCamera*>(active3D)) {
            ImGui::TextDisabled("現在の3Dカメラ: DebugCamera (注視点追従)");
        } else {
            ImGui::TextDisabled("現在の3Dカメラは追従モジュール未対応です。");
        }

        if (!statusMessage_.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", statusMessage_.c_str());
        }
    }

    GameObject* CameraFollowEditorModule::FindTargetObject(const CameraEditorContext& context) const
    {
        if (!context.gameObjectManager || targetObjectName_.empty()) {
            return nullptr;
        }

        // 登録済みオブジェクト名を線形走査し、追従先を解決する。
        for (const auto& obj : context.gameObjectManager->GetAllObjects()) {
            if (!obj) {
                continue;
            }
            if (obj->GetName() == targetObjectName_) {
                return obj.get();
            }
        }

        return nullptr;
    }

    void CameraFollowEditorModule::ApplyToReleaseCamera(const CameraEditorContext& context, const Vector3& targetPosition) const
    {
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        auto* releaseCamera = dynamic_cast<Camera*>(active3D);
        if (!releaseCamera) {
            return;
        }

        // 位置追従と注視を個別設定できるように分離して適用する。
        if (followEnabled_) {
            const Vector3 desiredPosition = targetPosition + followOffset_;
            const Vector3 current = releaseCamera->GetTranslate();
            releaseCamera->SetTranslate(LerpVector3(current, desiredPosition, followSmoothing_));
        }

        if (lookAtEnabled_) {
            releaseCamera->LookAt(targetPosition + lookAtOffset_);
        }
    }

    void CameraFollowEditorModule::ApplyToDebugCamera(const CameraEditorContext& context, const Vector3& targetPosition) const
    {
        ICamera* active3D = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        auto* debugCamera = dynamic_cast<DebugCamera*>(active3D);
        if (!debugCamera) {
            return;
        }

        // DebugCameraは注視点中心のカメラなので、追従/注視とも注視点移動として扱う。
        if (followEnabled_ || lookAtEnabled_) {
            const Vector3 desiredTarget = targetPosition + lookAtOffset_;
            const Vector3 currentTarget = debugCamera->GetTarget();
            debugCamera->SetTarget(LerpVector3(currentTarget, desiredTarget, followSmoothing_));
        }
    }

    Vector3 CameraFollowEditorModule::LerpVector3(const Vector3& from, const Vector3& to, float t)
    {
        const float clampedT = std::clamp(t, 0.0f, 1.0f);
        return {
            from.x + (to.x - from.x) * clampedT,
            from.y + (to.y - from.y) * clampedT,
            from.z + (to.z - from.z) * clampedT
        };
    }
}

#endif // _DEBUG
