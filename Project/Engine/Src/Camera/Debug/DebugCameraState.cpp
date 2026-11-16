#include "pch.h"
#include "DebugCameraState.h"

#include "Camera/Camera.h"
#include "Camera/Control/OrbitFlyController.h"
#include "Utility/JsonManager/JsonFields.h"

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
        constexpr auto kSnapshotFields = [](auto& snapshot, auto&& field) {
            field("rotationSensitivity", snapshot.settings.rotationSensitivity);
            field("panSensitivity", snapshot.settings.panSensitivity);
            field("zoomSensitivity", snapshot.settings.zoomSensitivity);
            field("minDistance", snapshot.settings.minDistance);
            field("maxDistance", snapshot.settings.maxDistance);
            field("invertY", snapshot.settings.invertY);
            field("smoothMovement", snapshot.settings.smoothMovement);
            field("smoothingFactor", snapshot.settings.smoothingFactor);
            field("flySpeed", snapshot.settings.flySpeed);
            field("flySpeedBoost", snapshot.settings.flySpeedBoost);
            field("maxHorizontalExtent", snapshot.settings.maxHorizontalExtent);
            field("minHeight", snapshot.settings.minHeight);
            field("maxHeight", snapshot.settings.maxHeight);
            field("target", snapshot.state.target);
            field("distance", snapshot.state.distance);
            field("pitch", snapshot.state.pitch);
            field("yaw", snapshot.state.yaw);
            field("fov", snapshot.fov);
            field("nearClip", snapshot.nearClip);
            field("farClip", snapshot.farClip);
        };
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
        JsonFields::WriteFields(out, Stored(), kSnapshotFields);
    }

    void DebugCameraState::Load(const nlohmann::json& in)
    {
        JsonFields::ReadFields(in, Stored(), kSnapshotFields);
        ++StoredRevision();
    }

    uint64_t DebugCameraState::GetRevision()
    {
        return StoredRevision();
    }
}
