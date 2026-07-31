#include "pch.h"
#include "CameraSequenceAssetIO.h"

#ifdef USE_IMGUI

#include "Utility/JsonManager/JsonManager.h"
#include "Camera/Control/OrbitFlyController.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief スナップショットをJSONへ変換
        json SnapshotToJson(const CameraSnapshot& snapshot)
        {
            // カメラが 1 種類になったため、スナップショットは常に Transform + 投影パラメータ。
            // 旧フォーマット（isDebugCamera + target/distance/pitch/yaw）も読めるようにしてある。
            json jsonData;
            jsonData["position"] = JsonManager::Vector3ToJson(snapshot.position);
            jsonData["rotation"] = JsonManager::Vector3ToJson(snapshot.rotation);
            jsonData["scale"] = JsonManager::Vector3ToJson(snapshot.scale);

            json paramsJson;
            paramsJson["fov"] = snapshot.parameters.fov;
            paramsJson["nearClip"] = snapshot.parameters.nearClip;
            paramsJson["farClip"] = snapshot.parameters.farClip;
            paramsJson["aspectRatio"] = snapshot.parameters.aspectRatio;
            jsonData["parameters"] = paramsJson;
            return jsonData;
        }

        /// @brief JSONからスナップショットを復元
        CameraSnapshot JsonToSnapshot(const json& jsonData)
        {
            CameraSnapshot snapshot{};

            // 旧フォーマット（軌道パラメータで保存されたクリップ）は、注視点・距離・角度から
            // 視点位置と姿勢を復元して新形式へ移す。
            if (JsonManager::SafeGet(jsonData, "isDebugCamera", false) && jsonData.contains("target")) {
                const Vector3 target = JsonManager::JsonToVector3(jsonData["target"]);
                const float distance = JsonManager::SafeGet(jsonData, "distance", 20.0f);
                const float pitch = JsonManager::SafeGet(jsonData, "pitch", 0.25f);
                const float yaw = JsonManager::SafeGet(jsonData, "yaw", 3.14159265359f);

                snapshot.position = {
                    target.x + distance * std::cosf(pitch) * std::sinf(yaw),
                    target.y + distance * std::sinf(pitch),
                    target.z + distance * std::cosf(pitch) * std::cosf(yaw)
                };
                // 注視点を向くオイラー角（OrbitFlyController::ApplyTo と同じ変換）
                snapshot.rotation = { pitch, OrbitFlyController::NormalizeAngle(yaw + 3.14159265359f), 0.0f };
                snapshot.scale = { 1.0f, 1.0f, 1.0f };
            } else {
                snapshot.position = JsonManager::JsonToVector3(jsonData["position"]);
                snapshot.rotation = JsonManager::JsonToVector3(jsonData["rotation"]);
                snapshot.scale = JsonManager::JsonToVector3(jsonData["scale"]);
            }

            if (jsonData.contains("parameters")) {
                const auto& params = jsonData["parameters"];
                snapshot.parameters.fov = JsonManager::SafeGet(params, "fov", 0.45f);
                snapshot.parameters.nearClip = JsonManager::SafeGet(params, "nearClip", 0.1f);
                snapshot.parameters.farClip = JsonManager::SafeGet(params, "farClip", 1000.0f);
                snapshot.parameters.aspectRatio = JsonManager::SafeGet(params, "aspectRatio", 0.0f);
            }

            return snapshot;
        }

        /// @brief ショット情報をJSONへ変換
        json ShotToJson(const CameraSequenceShot& shot)
        {
            json jsonData;
            jsonData["name"] = shot.name;
            jsonData["startTime"] = shot.startTime;
            jsonData["endTime"] = shot.endTime;
            jsonData["enabled"] = shot.enabled;
            jsonData["transitionType"] = static_cast<int>(shot.transitionType);
            jsonData["blendDuration"] = shot.blendDuration;
            return jsonData;
        }

        /// @brief JSONからショット情報を復元
        CameraSequenceShot JsonToShot(const json& jsonData)
        {
            CameraSequenceShot shot{};
            shot.name = JsonManager::SafeGet(jsonData, "name", std::string("ショット"));
            shot.startTime = JsonManager::SafeGet(jsonData, "startTime", 0.0f);
            shot.endTime = JsonManager::SafeGet(jsonData, "endTime", 1.0f);
            shot.enabled = JsonManager::SafeGet(jsonData, "enabled", true);

            const int transition = JsonManager::SafeGet(jsonData, "transitionType", 0);
            shot.transitionType = (transition == 1)
                ? CameraSequenceTransitionType::Blend
                : CameraSequenceTransitionType::Cut;
            shot.blendDuration = JsonManager::SafeGet(jsonData, "blendDuration", 0.2f);
            return shot;
        }
    }

    std::vector<std::string> CameraSequenceAssetIO::GetSequenceFileList(const std::string& directoryPath)
    {
        std::vector<std::string> fileList;

        if (!std::filesystem::exists(directoryPath)) {
            return fileList;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                fileList.push_back(entry.path().filename().string());
            }
        }

        std::sort(fileList.begin(), fileList.end());
        return fileList;
    }

    bool CameraSequenceAssetIO::Save(const std::string& filePath, const CameraSequenceAsset& asset)
    {
        json root;
        root["version"] = asset.version;
        root["timelineLength"] = asset.timelineLength;
        root["easingTypeIndex"] = asset.easingTypeIndex;
        root["shotsEnabled"] = asset.shotsEnabled;

        json keyframesJson = json::array();
        for (const auto& key : asset.keyframes) {
            json keyJson;
            keyJson["time"] = key.time;
            keyJson["snapshot"] = SnapshotToJson(key.snapshot);
            keyframesJson.push_back(keyJson);
        }
        root["keyframes"] = keyframesJson;

        json shotsJson = json::array();
        for (const auto& shot : asset.shots) {
            shotsJson.push_back(ShotToJson(shot));
        }
        root["shots"] = shotsJson;

        return JsonManager::GetInstance().SaveJson(filePath, root);
    }

    bool CameraSequenceAssetIO::Load(const std::string& filePath, CameraSequenceAsset& outAsset)
    {
        if (!JsonManager::GetInstance().FileExists(filePath)) {
            return false;
        }

        const json root = JsonManager::GetInstance().LoadJson(filePath);
        if (root.empty()) {
            return false;
        }

        CameraSequenceAsset asset{};
        asset.version = JsonManager::SafeGet(root, "version", std::string("1.0"));
        asset.timelineLength = JsonManager::SafeGet(root, "timelineLength", 10.0f);
        asset.easingTypeIndex = JsonManager::SafeGet(root, "easingTypeIndex", 0);
        asset.shotsEnabled = JsonManager::SafeGet(root, "shotsEnabled", true);

        if (asset.timelineLength < 0.1f) {
            asset.timelineLength = 0.1f;
        }

        if (root.contains("keyframes") && root["keyframes"].is_array()) {
            for (const auto& keyJson : root["keyframes"]) {
                CameraSequenceAsset::Keyframe key{};
                key.time = JsonManager::SafeGet(keyJson, "time", 0.0f);
                if (keyJson.contains("snapshot")) {
                    key.snapshot = JsonToSnapshot(keyJson["snapshot"]);
                }
                asset.keyframes.push_back(key);
            }
        }

        std::sort(asset.keyframes.begin(), asset.keyframes.end(),
            [](const CameraSequenceAsset::Keyframe& lhs, const CameraSequenceAsset::Keyframe& rhs) {
                return lhs.time < rhs.time;
            });

        if (root.contains("shots") && root["shots"].is_array()) {
            for (const auto& shotJson : root["shots"]) {
                CameraSequenceShot shot = JsonToShot(shotJson);
                shot.startTime = std::clamp(shot.startTime, 0.0f, asset.timelineLength);
                shot.endTime = std::clamp(shot.endTime, 0.0f, asset.timelineLength);
                if (shot.endTime <= shot.startTime) {
                    shot.endTime = std::clamp(shot.startTime + 0.01f, 0.01f, asset.timelineLength);
                }
                if (shot.blendDuration < 0.0f) {
                    shot.blendDuration = 0.0f;
                }
                asset.shots.push_back(shot);
            }
        }

        outAsset = std::move(asset);
        return !outAsset.keyframes.empty();
    }
}

#endif // _DEBUG
