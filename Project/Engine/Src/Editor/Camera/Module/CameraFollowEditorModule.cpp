#include "pch.h"
#include "CameraFollowEditorModule.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include <algorithm>

#include "Camera/CameraManager.h"
#include "Camera/Camera.h"
#include "Camera/Camera.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"

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

        // 軌道コントローラが付いているカメラは注視点を動かす（位置を直接書いても
        // 次フレームでコントローラに上書きされるため）。付いていなければ Transform を直接動かす。
        if (context.cameraManager->GetActiveOrbitController()) {
            ApplyToOrbitCamera(context, targetPosition);
        } else {
            ApplyToFreeCamera(context, targetPosition);
        }
    }

    void CameraFollowEditorModule::Draw(const CameraEditorContext& context)
    {
        if (!context.cameraManager) {
            return;
        }

        UI::Widgets::ToggleSwitch("追従/注視を有効", &enabled_);
        UI::Widgets::ToggleSwitch("位置を追従", &followEnabled_);
        UI::SameLine();
        UI::Widgets::ToggleSwitch("対象を注視", &lookAtEnabled_);

        UI::Separator();

        if (!context.gameObjectManager) {
            UI::Hint("GameObjectManagerが未設定のため追従対象を選択できません。");
            return;
        }

        // 対象候補リストを毎フレーム構築し、最新のシーン状態を反映する。
        const auto& objects = context.gameObjectManager->GetAllObjects();
        if (objects.empty()) {
            UI::Hint("シーン内に追従対象オブジェクトがありません。");
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

        UI::DragVec3("追従オフセット", followOffset_, 0.05f);
        UI::DragVec3("注視オフセット", lookAtOffset_, 0.05f);
        UI::SliderFloat("追従スムージング", followSmoothing_, 0.01f, 1.0f, "%.2f");

        if (followSmoothing_ < 0.01f) {
            followSmoothing_ = 0.01f;
        }

        if (!context.cameraManager->GetActiveCamera(CameraType::Camera3D)) {
            UI::Hint("アクティブな3Dカメラがありません。");
        } else if (context.cameraManager->GetActiveOrbitController()) {
            UI::Hint("現在の3Dカメラ: 軌道操作つき (注視点追従)");
        } else {
            UI::Hint("現在の3Dカメラ: 自由カメラ (追従+注視対応)");
        }

        if (!statusMessage_.empty()) {
            UI::Separator();
            UI::Hint(statusMessage_.c_str());
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

    void CameraFollowEditorModule::ApplyToFreeCamera(const CameraEditorContext& context, const Vector3& targetPosition) const
    {
        Camera* camera = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
        if (!camera) {
            return;
        }

        // 位置追従と注視を個別設定できるように分離して適用する。
        if (followEnabled_) {
            const Vector3 desiredPosition = targetPosition + followOffset_;
            const Vector3 current = camera->GetTranslate();
            camera->SetTranslate(LerpVector3(current, desiredPosition, followSmoothing_));
        }

        if (lookAtEnabled_) {
            camera->LookAt(targetPosition + lookAtOffset_);
        }
    }

    void CameraFollowEditorModule::ApplyToOrbitCamera(const CameraEditorContext& context, const Vector3& targetPosition) const
    {
        auto* orbit = context.cameraManager->GetActiveOrbitController();
        if (!orbit) {
            return;
        }

        // 軌道カメラは注視点中心なので、追従/注視とも注視点移動として扱う。
        if (followEnabled_ || lookAtEnabled_) {
            const Vector3 desiredTarget = targetPosition + lookAtOffset_;
            const Vector3 currentTarget = orbit->GetTarget();
            orbit->SetTarget(LerpVector3(currentTarget, desiredTarget, followSmoothing_));
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
