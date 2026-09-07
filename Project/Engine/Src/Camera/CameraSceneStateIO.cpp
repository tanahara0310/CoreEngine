#include "pch.h"
#include "CameraSceneStateIO.h"

#include "Camera/Camera.h"
#include "Camera/Rig/CameraRig.h"
#include "Camera/CameraManager.h"
#include "Utility/JsonManager/JsonManager.h"

#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief シーンフォルダ（SceneSaveSystem がオブジェクトを置く場所と同じ）
        constexpr const char* kSceneRoot = "Application/Assets/Scenes/";

        json SnapshotToJson(const CameraSnapshot& snapshot)
        {
            json data;
            data["position"] = JsonManager::Vector3ToJson(snapshot.position);
            data["rotation"] = JsonManager::Vector3ToJson(snapshot.rotation);
            data["scale"] = JsonManager::Vector3ToJson(snapshot.scale);
            data["fov"] = snapshot.parameters.fov;
            data["nearClip"] = snapshot.parameters.nearClip;
            data["farClip"] = snapshot.parameters.farClip;
            data["aspectRatio"] = snapshot.parameters.aspectRatio;
            data["projectionType"] = static_cast<int>(snapshot.parameters.projectionType);
            return data;
        }

        CameraSnapshot JsonToSnapshot(const json& data)
        {
            CameraSnapshot snapshot{};
            snapshot.position = JsonManager::SafeGetVector3(data, "position", snapshot.position);
            snapshot.rotation = JsonManager::SafeGetVector3(data, "rotation", snapshot.rotation);
            snapshot.scale = JsonManager::SafeGetVector3(data, "scale", snapshot.scale);
            snapshot.parameters.fov = JsonManager::SafeGet(data, "fov", snapshot.parameters.fov);
            snapshot.parameters.nearClip = JsonManager::SafeGet(data, "nearClip", snapshot.parameters.nearClip);
            snapshot.parameters.farClip = JsonManager::SafeGet(data, "farClip", snapshot.parameters.farClip);
            snapshot.parameters.aspectRatio = JsonManager::SafeGet(data, "aspectRatio", snapshot.parameters.aspectRatio);

            const int projection = JsonManager::SafeGet(data, "projectionType",
                static_cast<int>(CameraProjectionType::Perspective));
            snapshot.parameters.projectionType = (projection == static_cast<int>(CameraProjectionType::Orthographic))
                ? CameraProjectionType::Orthographic
                : CameraProjectionType::Perspective;

            // 壊れた値で射影行列が破綻しないよう、読み込み時に均しておく。
            if (snapshot.parameters.nearClip < 0.001f) {
                snapshot.parameters.nearClip = 0.001f;
            }
            if (snapshot.parameters.farClip <= snapshot.parameters.nearClip) {
                snapshot.parameters.farClip = snapshot.parameters.nearClip + 0.001f;
            }
            return snapshot;
        }
    }

    std::string CameraSceneStateIO::GetFilePath(const std::string& sceneName)
    {
        return (std::filesystem::path(kSceneRoot) / sceneName / "_camera.json").string();
    }

    CameraSceneState CameraSceneStateIO::Capture(const CameraManager& cameraManager)
    {
        CameraSceneState state{};
        state.sceneCameraName = cameraManager.GetSceneCameraName();
        state.gameCameraName = cameraManager.GetGameCameraName();

        // 保存した時点で動かしているリグを、このシーンの開始リグとして控える。
        // 何も動かしていなければ空になり、次回は自動起動しない（外し方も兼ねる）。
        state.startupRigName = CameraRig::GetActiveName();

        for (const auto& [name, camera] : cameraManager.GetAllCameras()) {
            if (!camera) {
                continue;
            }

            CameraSceneStateEntry entry{};
            entry.name = name;
            entry.snapshot = camera->CaptureSnapshot(name);

            // 軌道コントローラ付きのカメラは、姿勢ではなく軌道状態が正本。
            if (auto* orbit = cameraManager.GetControllerAs<OrbitFlyController>(name)) {
                entry.hasOrbitState = true;
                entry.orbitState = orbit->GetState();
            }

            state.cameras.push_back(std::move(entry));
        }

        return state;
    }

    void CameraSceneStateIO::Apply(const CameraSceneState& state, CameraManager& cameraManager)
    {
        if (!state.sceneCameraName.empty()) {
            cameraManager.SetSceneCameraName(state.sceneCameraName);
        }
        if (!state.gameCameraName.empty()) {
            cameraManager.SetGameCameraName(state.gameCameraName);
        }

        for (const auto& entry : state.cameras) {
            Camera* camera = cameraManager.GetCamera(entry.name);
            if (!camera) {
                // 保存時にあって今は無いカメラ。シーン構成が変わっただけなので黙って飛ばす。
                continue;
            }

            camera->RestoreSnapshot(entry.snapshot);

            // 軌道コントローラは毎フレーム Transform を書き直す。状態を戻してから
            // ApplyTo で姿勢まで確定させないと、復元した構図が次のフレームで消える。
            if (entry.hasOrbitState) {
                if (auto* orbit = cameraManager.GetControllerAs<OrbitFlyController>(entry.name)) {
                    orbit->SetState(entry.orbitState);
                    orbit->ApplyTo(*camera);
                }
            }

            camera->UpdateMatrix();
        }
    }

    bool CameraSceneStateIO::Save(const std::string& sceneName, const CameraManager& cameraManager)
    {
        if (sceneName.empty()) {
            return false;
        }

        const CameraSceneState state = Capture(cameraManager);

        json root;
        root["version"] = "1.0";
        root["sceneCameraName"] = state.sceneCameraName;
        root["gameCameraName"] = state.gameCameraName;
        root["startupRigName"] = state.startupRigName;

        json camerasJson = json::array();
        for (const auto& entry : state.cameras) {
            json cameraJson;
            cameraJson["name"] = entry.name;
            cameraJson["snapshot"] = SnapshotToJson(entry.snapshot);

            if (entry.hasOrbitState) {
                json orbitJson;
                orbitJson["target"] = JsonManager::Vector3ToJson(entry.orbitState.target);
                orbitJson["distance"] = entry.orbitState.distance;
                orbitJson["pitch"] = entry.orbitState.pitch;
                orbitJson["yaw"] = entry.orbitState.yaw;
                cameraJson["orbit"] = orbitJson;
            }

            camerasJson.push_back(cameraJson);
        }
        root["cameras"] = camerasJson;

        const std::string path = GetFilePath(sceneName);
        JsonManager::GetInstance().CreateJsonDirectory(
            std::filesystem::path(path).parent_path().string());
        return JsonManager::GetInstance().SaveJson(path, root);
    }

    bool CameraSceneStateIO::Load(const std::string& sceneName, CameraManager& cameraManager)
    {
        if (sceneName.empty()) {
            return false;
        }

        const std::string path = GetFilePath(sceneName);
        if (!JsonManager::GetInstance().FileExists(path)) {
            return false;
        }

        const json root = JsonManager::GetInstance().LoadJson(path);
        if (root.empty() || !root.contains("cameras") || !root["cameras"].is_array()) {
            return false;
        }

        CameraSceneState state{};
        state.sceneCameraName = JsonManager::SafeGet(root, "sceneCameraName", std::string());
        state.gameCameraName = JsonManager::SafeGet(root, "gameCameraName", std::string());
        state.startupRigName = JsonManager::SafeGet(root, "startupRigName", std::string());

        for (const auto& cameraJson : root["cameras"]) {
            CameraSceneStateEntry entry{};
            entry.name = JsonManager::SafeGet(cameraJson, "name", std::string());
            if (entry.name.empty()) {
                continue;
            }

            if (cameraJson.contains("snapshot")) {
                entry.snapshot = JsonToSnapshot(cameraJson["snapshot"]);
            }

            if (cameraJson.contains("orbit")) {
                const auto& orbitJson = cameraJson["orbit"];
                entry.hasOrbitState = true;
                entry.orbitState.target = JsonManager::SafeGetVector3(orbitJson, "target", entry.orbitState.target);
                entry.orbitState.distance = JsonManager::SafeGet(orbitJson, "distance", entry.orbitState.distance);
                entry.orbitState.pitch = JsonManager::SafeGet(orbitJson, "pitch", entry.orbitState.pitch);
                entry.orbitState.yaw = JsonManager::SafeGet(orbitJson, "yaw", entry.orbitState.yaw);
            }

            state.cameras.push_back(std::move(entry));
        }

        if (state.cameras.empty()) {
            return false;
        }

        Apply(state, cameraManager);
        return true;
    }
    std::string CameraSceneStateIO::LoadStartupRigName(const std::string& sceneName)
    {
        if (sceneName.empty()) {
            return {};
        }

        const std::string path = GetFilePath(sceneName);
        if (!JsonManager::GetInstance().FileExists(path)) {
            return {};
        }

        const json root = JsonManager::GetInstance().LoadJson(path);
        if (root.empty()) {
            return {};
        }
        return JsonManager::SafeGet(root, "startupRigName", std::string());
    }
}
