#include "pch.h"
#include "DebugCameraSettingsSection.h"
#include "Camera/Debug/DebugCamera.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    void DebugCameraSettingsSection::Serialize(json& out) const
    {
        if (!camera_) {
            return;
        }

        // 操作設定
        // useGameView はコード制御フラグ（BaseScene::SetupCamera が設定）のため保存しない
        const auto& s = camera_->GetSettings();
        out["rotationSensitivity"] = s.rotationSensitivity;
        out["panSensitivity"] = s.panSensitivity;
        out["zoomSensitivity"] = s.zoomSensitivity;
        out["minDistance"] = s.minDistance;
        out["maxDistance"] = s.maxDistance;
        out["invertY"] = s.invertY;
        out["smoothMovement"] = s.smoothMovement;
        out["smoothingFactor"] = s.smoothingFactor;
        out["flySpeed"] = s.flySpeed;
        out["flySpeedBoost"] = s.flySpeedBoost;
        out["maxHorizontalExtent"] = s.maxHorizontalExtent;
        out["minHeight"] = s.minHeight;
        out["maxHeight"] = s.maxHeight;

        // 姿勢
        out["target"] = JsonManager::Vector3ToJson(camera_->GetTarget());
        out["distance"] = camera_->GetDistance();
        out["pitch"] = camera_->GetPitch();
        out["yaw"] = camera_->GetYaw();

        // 投影パラメータ
        const CameraParameters params = camera_->GetParameters();
        out["fov"] = params.fov;
        out["nearClip"] = params.nearClip;
        out["farClip"] = params.farClip;
        out["aspectRatio"] = params.aspectRatio;
    }

    void DebugCameraSettingsSection::Deserialize(const json& in)
    {
        if (!camera_) {
            return;
        }

        // 操作設定（欠損キーは現在値＝コードデフォルトを維持）
        auto s = camera_->GetSettings();
        s.rotationSensitivity = JsonManager::SafeGet(in, "rotationSensitivity", s.rotationSensitivity);
        s.panSensitivity = JsonManager::SafeGet(in, "panSensitivity", s.panSensitivity);
        s.zoomSensitivity = JsonManager::SafeGet(in, "zoomSensitivity", s.zoomSensitivity);
        s.minDistance = JsonManager::SafeGet(in, "minDistance", s.minDistance);
        s.maxDistance = JsonManager::SafeGet(in, "maxDistance", s.maxDistance);
        s.invertY = JsonManager::SafeGet(in, "invertY", s.invertY);
        s.smoothMovement = JsonManager::SafeGet(in, "smoothMovement", s.smoothMovement);
        s.smoothingFactor = JsonManager::SafeGet(in, "smoothingFactor", s.smoothingFactor);
        s.flySpeed = JsonManager::SafeGet(in, "flySpeed", s.flySpeed);
        s.flySpeedBoost = JsonManager::SafeGet(in, "flySpeedBoost", s.flySpeedBoost);
        s.maxHorizontalExtent = JsonManager::SafeGet(in, "maxHorizontalExtent", s.maxHorizontalExtent);
        s.minHeight = JsonManager::SafeGet(in, "minHeight", s.minHeight);
        s.maxHeight = JsonManager::SafeGet(in, "maxHeight", s.maxHeight);
        camera_->SetSettings(s);

        // 姿勢と投影パラメータは RestoreSnapshot 経由で適用する
        // （クランプ・スムーズ移動値の同期・行列更新までまとめて行われる）
        CameraSnapshot snapshot = camera_->CaptureSnapshot("EditorSettings");
        snapshot.target = JsonManager::SafeGetVector3(in, "target", snapshot.target);
        snapshot.distance = JsonManager::SafeGet(in, "distance", snapshot.distance);
        snapshot.pitch = JsonManager::SafeGet(in, "pitch", snapshot.pitch);
        snapshot.yaw = JsonManager::SafeGet(in, "yaw", snapshot.yaw);
        snapshot.parameters.fov = JsonManager::SafeGet(in, "fov", snapshot.parameters.fov);
        snapshot.parameters.nearClip = JsonManager::SafeGet(in, "nearClip", snapshot.parameters.nearClip);
        snapshot.parameters.farClip = JsonManager::SafeGet(in, "farClip", snapshot.parameters.farClip);
        snapshot.parameters.aspectRatio = JsonManager::SafeGet(in, "aspectRatio", snapshot.parameters.aspectRatio);
        camera_->RestoreSnapshot(snapshot);
    }
}
