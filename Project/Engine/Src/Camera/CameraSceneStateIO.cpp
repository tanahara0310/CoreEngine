#include "pch.h"
#include "CameraSceneStateIO.h"

#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Utility/JsonManager/JsonFields.h"
#include "Utility/JsonManager/JsonManager.h"

#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief シーンごとのエディタの視点を置くフォルダ（自分だけの状態）
        constexpr const char* kViewRoot = "Application/Saved/EditorSettings/SceneViews/";

        /// @brief 保存された投影の番号として読める範囲（0〜最後の値）
        constexpr int kProjectionTypeCount = static_cast<int>(CameraProjectionType::Orthographic) + 1;

        /// @brief スナップショットの項目
        constexpr auto kSnapshotFields = [](auto& snapshot, auto&& field) {
            field("position", snapshot.position);
            field("rotation", snapshot.rotation);
            field("scale", snapshot.scale);
            field("fov", snapshot.parameters.fov);
            field("nearClip", snapshot.parameters.nearClip);
            field("farClip", snapshot.parameters.farClip);
            field("orthographicSize", snapshot.parameters.orthographicSize);
            field("aspectRatio", snapshot.parameters.aspectRatio);
            field("projectionType", snapshot.parameters.projectionType,
                kProjectionTypeCount, CameraProjectionType::Perspective);
        };

        /// @brief 軌道の状態の項目（"orbit" の欄。2D の視点は別に読み書きする）
        constexpr auto kOrbitStateFields = [](auto& orbitState, auto&& field) {
            field("target", orbitState.target);
            field("distance", orbitState.distance);
            field("pitch", orbitState.pitch);
            field("yaw", orbitState.yaw);
        };

        CameraSnapshot JsonToSnapshot(const json& data)
        {
            CameraSnapshot snapshot = JsonFields::FromJsonObject<CameraSnapshot>(data, kSnapshotFields);

            // 壊れた値で射影行列が破綻しないよう、読み込み時に均しておく。
            if (snapshot.parameters.nearClip < 0.001f) {
                snapshot.parameters.nearClip = 0.001f;
            }
            if (snapshot.parameters.farClip <= snapshot.parameters.nearClip) {
                snapshot.parameters.farClip = snapshot.parameters.nearClip + 0.001f;
            }
            return snapshot;
        }

        json View2DToJson(const OrbitFlyController::View2DState& view2D)
        {
            json data;
            data["enabled"] = view2D.enabled;
            data["center"] = json::array({ view2D.center.x, view2D.center.y });
            data["orthographicSize"] = view2D.orthographicSize;
            return data;
        }

        OrbitFlyController::View2DState JsonToView2D(const json& data)
        {
            OrbitFlyController::View2DState view2D{};
            view2D.enabled = JsonManager::SafeGet(data, "enabled", view2D.enabled);
            if (data.contains("center") && data["center"].is_array() && data["center"].size() == 2
                && data["center"][0].is_number() && data["center"][1].is_number()) {
                view2D.center = { data["center"][0].get<float>(), data["center"][1].get<float>() };
            }
            view2D.orthographicSize = JsonManager::SafeGet(data, "orthographicSize", view2D.orthographicSize);
            return view2D;
        }
    }

    std::string CameraSceneStateIO::GetFilePath(const std::string& sceneName)
    {
        return std::string(kViewRoot) + sceneName + ".json";
    }

    CameraSceneState CameraSceneStateIO::Capture(const CameraManager& cameraManager)
    {
        CameraSceneState state{};
        state.sceneCameraName = cameraManager.GetSceneCameraName();
        state.gameCameraName = cameraManager.GetGameCameraName();

        for (const auto& [name, camera] : cameraManager.GetAllCameras()) {
            if (!camera) {
                continue;
            }

            // オブジェクトが持つカメラは、構図もレンズもオブジェクトの保存データが正本。
            // ここに書くと 2 か所に同じ値が残り、どちらが勝つか読めなくなる
            if (cameraManager.IsObjectOwnedCamera(name)) {
                continue;
            }

            CameraSceneStateEntry entry{};
            entry.name = name;
            entry.snapshot = camera->CaptureSnapshot(name);

            // 軌道コントローラ付きのカメラは、姿勢ではなく軌道状態が正本。
            if (auto* orbit = cameraManager.GetControllerAs<OrbitFlyController>(name)) {
                entry.hasOrbitState = true;
                entry.orbitState = orbit->GetState();
                entry.view2D = orbit->GetView2DState();
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
                    orbit->SetView2DState(entry.view2D);
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

        json camerasJson = json::array();
        for (const auto& entry : state.cameras) {
            json cameraJson;
            cameraJson["name"] = entry.name;
            cameraJson["snapshot"] = JsonFields::ToJsonObject(entry.snapshot, kSnapshotFields);

            if (entry.hasOrbitState) {
                json orbitJson = JsonFields::ToJsonObject(entry.orbitState, kOrbitStateFields);
                orbitJson["view2D"] = View2DToJson(entry.view2D);
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
                JsonFields::ReadFields(orbitJson, entry.orbitState, kOrbitStateFields);
                if (orbitJson.contains("view2D")) {
                    entry.view2D = JsonToView2D(orbitJson["view2D"]);
                }
            }

            state.cameras.push_back(std::move(entry));
        }

        if (state.cameras.empty()) {
            return false;
        }

        Apply(state, cameraManager);
        return true;
    }
}
