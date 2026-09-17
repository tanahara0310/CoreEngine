#include "pch.h"
#include "DebugCameraState.h"

#include "Camera/Camera.h"
#include "Camera/Control/OrbitFlyController.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    namespace
    {
        /// @brief 控えている設定・姿勢・投影
        struct Snapshot
        {
            OrbitFlyController::Settings settings{};
            OrbitFlyController::OrbitState state{};
            float fov = CameraParameters{}.fov;
            float nearClip = CameraParameters{}.nearClip;
            float farClip = CameraParameters{}.farClip;

            bool operator==(const Snapshot&) const = default;
        };

        Snapshot& Stored()
        {
            static Snapshot snapshot;
            return snapshot;
        }

        uint64_t& StoredRevision()
        {
            static uint64_t revision = 0;
            return revision;
        }

        /// @brief 保存する項目を保存キーと一緒に 1 つずつ渡す
        template <class SnapshotType, class Visitor>
        void VisitFields(SnapshotType& snapshot, Visitor&& visit)
        {
            visit("rotationSensitivity", snapshot.settings.rotationSensitivity);
            visit("panSensitivity", snapshot.settings.panSensitivity);
            visit("zoomSensitivity", snapshot.settings.zoomSensitivity);
            visit("minDistance", snapshot.settings.minDistance);
            visit("maxDistance", snapshot.settings.maxDistance);
            visit("invertY", snapshot.settings.invertY);
            visit("smoothMovement", snapshot.settings.smoothMovement);
            visit("smoothingFactor", snapshot.settings.smoothingFactor);
            visit("flySpeed", snapshot.settings.flySpeed);
            visit("flySpeedBoost", snapshot.settings.flySpeedBoost);
            visit("maxHorizontalExtent", snapshot.settings.maxHorizontalExtent);
            visit("minHeight", snapshot.settings.minHeight);
            visit("maxHeight", snapshot.settings.maxHeight);
            visit("target", snapshot.state.target);
            visit("distance", snapshot.state.distance);
            visit("pitch", snapshot.state.pitch);
            visit("yaw", snapshot.state.yaw);
            visit("fov", snapshot.fov);
            visit("nearClip", snapshot.nearClip);
            visit("farClip", snapshot.farClip);
        }

        nlohmann::json ToJsonValue(float value) { return value; }
        nlohmann::json ToJsonValue(bool value) { return value; }
        nlohmann::json ToJsonValue(const Vector3& value) { return JsonManager::Vector3ToJson(value); }

        void ReadJsonValue(const nlohmann::json& in, const char* key, float& value)
        {
            value = JsonManager::SafeGet(in, key, value);
        }
        void ReadJsonValue(const nlohmann::json& in, const char* key, bool& value)
        {
            value = JsonManager::SafeGet(in, key, value);
        }
        void ReadJsonValue(const nlohmann::json& in, const char* key, Vector3& value)
        {
            value = JsonManager::SafeGetVector3(in, key, value);
        }
    }

    void DebugCameraState::RestoreTo(Camera& camera, OrbitFlyController& controller)
    {
        const Snapshot& stored = Stored();
        controller.SetSettings(stored.settings);

        // SetState がクランプとスムーズ値の同期までまとめて行う。
        // 移動範囲のクランプが効くよう、設定の適用より後に行う
        controller.SetState(stored.state);

        CameraParameters params = camera.GetParameters();
        params.fov = stored.fov;
        params.nearClip = stored.nearClip;
        params.farClip = stored.farClip;
        camera.SetParameters(params);

        controller.ApplyTo(camera);
        camera.UpdateMatrix();
    }

    void DebugCameraState::Capture(const Camera& camera, const OrbitFlyController& controller)
    {
        Snapshot current;
        current.settings = controller.GetSettings();
        current.state = controller.GetState();
        const CameraParameters params = camera.GetParameters();
        current.fov = params.fov;
        current.nearClip = params.nearClip;
        current.farClip = params.farClip;

        if (current == Stored()) {
            return;
        }
        Stored() = current;
        ++StoredRevision();
    }

    void DebugCameraState::Save(nlohmann::json& out)
    {
        VisitFields(Stored(), [&out](const char* key, const auto& value) {
            out[key] = ToJsonValue(value);
        });
    }

    void DebugCameraState::Load(const nlohmann::json& in)
    {
        VisitFields(Stored(), [&in](const char* key, auto& value) {
            ReadJsonValue(in, key, value);
        });
        ++StoredRevision();
    }

    uint64_t DebugCameraState::GetRevision()
    {
        return StoredRevision();
    }
}
