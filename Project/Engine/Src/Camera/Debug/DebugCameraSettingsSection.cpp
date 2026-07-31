#include "pch.h"
#include "DebugCameraSettingsSection.h"
#include "Camera/Camera.h"
#include "Camera/Control/OrbitFlyController.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    void DebugCameraSettingsSection::Serialize(json& out) const
    {
        if (!camera_ || !controller_) {
            return;
        }

        // 操作設定
        const auto& s = controller_->GetSettings();
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

        // 軌道状態（コントローラの内部状態）
        const auto& state = controller_->GetState();
        out["target"] = JsonManager::Vector3ToJson(state.target);
        out["distance"] = state.distance;
        out["pitch"] = state.pitch;
        out["yaw"] = state.yaw;

        // 投影パラメータ（カメラ本体）
        const CameraParameters params = camera_->GetParameters();
        out["fov"] = params.fov;
        out["nearClip"] = params.nearClip;
        out["farClip"] = params.farClip;
        out["aspectRatio"] = params.aspectRatio;
    }

    void DebugCameraSettingsSection::Deserialize(const json& in)
    {
        if (!camera_ || !controller_) {
            return;
        }

        // 操作設定（欠損キーは現在値＝コードデフォルトを維持）
        auto s = controller_->GetSettings();
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
        controller_->SetSettings(s);

        // 軌道状態（SetState がクランプとスムーズ値の同期までまとめて行う）
        auto state = controller_->GetState();
        state.target = JsonManager::SafeGetVector3(in, "target", state.target);
        state.distance = JsonManager::SafeGet(in, "distance", state.distance);
        state.pitch = JsonManager::SafeGet(in, "pitch", state.pitch);
        state.yaw = JsonManager::SafeGet(in, "yaw", state.yaw);
        controller_->SetState(state);

        // 投影パラメータ
        CameraParameters params = camera_->GetParameters();
        params.fov = JsonManager::SafeGet(in, "fov", params.fov);
        params.nearClip = JsonManager::SafeGet(in, "nearClip", params.nearClip);
        params.farClip = JsonManager::SafeGet(in, "farClip", params.farClip);
        params.aspectRatio = JsonManager::SafeGet(in, "aspectRatio", params.aspectRatio);
        camera_->SetParameters(params);

        // 復元直後に姿勢と行列を反映しておく（次フレームの Update を待たない）
        controller_->ApplyTo(*camera_);
        camera_->UpdateMatrix();
    }
}
