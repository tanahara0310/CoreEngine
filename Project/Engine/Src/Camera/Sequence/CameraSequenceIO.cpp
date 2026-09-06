#include "pch.h"
#include "CameraSequenceIO.h"

#include "Utility/JsonManager/JsonManager.h"
#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief スナップショットを JSON へ変換
        json SnapshotToJson(const CameraSnapshot& snapshot)
        {
            // カメラが 1 種類になったため、スナップショットは常に Transform + 投影パラメータ。
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

        /// @brief JSON からスナップショットを復元
        CameraSnapshot JsonToSnapshot(const json& jsonData)
        {
            CameraSnapshot snapshot{};

            // 旧フォーマット（軌道パラメータで保存されたクリップ）は、注視点・距離・角度から
            // 視点位置と姿勢を復元して新形式へ移す。
            if (JsonManager::SafeGet(jsonData, "isDebugCamera", false) && jsonData.contains("target")) {
                const Vector3 target = JsonManager::JsonToVector3(jsonData["target"]);
                const float distance = JsonManager::SafeGet(jsonData, "distance", 20.0f);
                const float pitch = JsonManager::SafeGet(jsonData, "pitch", 0.25f);
                const float yaw = JsonManager::SafeGet(jsonData, "yaw", MathCore::Constants::kPi);

                snapshot.position = {
                    target.x + distance * std::cosf(pitch) * std::sinf(yaw),
                    target.y + distance * std::sinf(pitch),
                    target.z + distance * std::cosf(pitch) * std::cosf(yaw)
                };
                // 注視点を向くオイラー角（OrbitFlyController::ApplyTo と同じ変換）
                snapshot.rotation = { pitch, MathCore::NormalizeAngle(yaw + MathCore::Constants::kPi), 0.0f };
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

        /// @brief 保存された番号を補間方式へ戻す（未知の値は直線扱い）
        CameraSequenceInterpolation ToInterpolation(int value)
        {
            switch (value) {
            case static_cast<int>(CameraSequenceInterpolation::Step):
                return CameraSequenceInterpolation::Step;
            case static_cast<int>(CameraSequenceInterpolation::Smooth):
                return CameraSequenceInterpolation::Smooth;
            default:
                return CameraSequenceInterpolation::Linear;
            }
        }

        /// @brief 保存された番号を向きの決め方へ戻す（未知の値は Euler 扱い）
        CameraSequenceAimMode ToAimMode(int value)
        {
            switch (value) {
            case static_cast<int>(CameraSequenceAimMode::LookAtPoint):
                return CameraSequenceAimMode::LookAtPoint;
            case static_cast<int>(CameraSequenceAimMode::LookAtObject):
                return CameraSequenceAimMode::LookAtObject;
            default:
                return CameraSequenceAimMode::Euler;
            }
        }

        /// @brief ショット情報を JSON へ変換
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

        /// @brief 保存された番号をイベント種別へ戻す（未知の値は Callback 扱い）
        /// @details 知らない種別を落とすと、新しいエンジンで作ったシーケンスを
        ///          古いエンジンで開いたときイベントが黙って消える。Callback へ倒して残す。
        CameraSequenceEventType ToEventType(int value)
        {
            switch (value) {
            case static_cast<int>(CameraSequenceEventType::Shake):
                return CameraSequenceEventType::Shake;
            case static_cast<int>(CameraSequenceEventType::Trauma):
                return CameraSequenceEventType::Trauma;
            case static_cast<int>(CameraSequenceEventType::TimeScale):
                return CameraSequenceEventType::TimeScale;
            default:
                return CameraSequenceEventType::Callback;
            }
        }

        /// @brief イベントを JSON へ変換
        json EventToJson(const CameraSequenceEvent& event)
        {
            json jsonData;
            jsonData["time"] = event.time;
            jsonData["type"] = static_cast<int>(event.type);
            jsonData["enabled"] = event.enabled;
            jsonData["name"] = event.name;
            jsonData["value"] = event.value;
            jsonData["duration"] = event.duration;
            return jsonData;
        }

        /// @brief JSON からイベントを復元
        CameraSequenceEvent JsonToEvent(const json& jsonData)
        {
            CameraSequenceEvent event{};
            event.time = JsonManager::SafeGet(jsonData, "time", 0.0f);
            event.type = ToEventType(JsonManager::SafeGet(jsonData, "type",
                static_cast<int>(CameraSequenceEventType::Shake)));
            event.enabled = JsonManager::SafeGet(jsonData, "enabled", true);
            event.name = JsonManager::SafeGet(jsonData, "name", std::string());
            event.value = JsonManager::SafeGet(jsonData, "value", 1.0f);
            event.duration = JsonManager::SafeGet(jsonData, "duration", 0.2f);
            return event;
        }

        /// @brief JSON からショット情報を復元
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

    std::vector<std::string> CameraSequenceIO::GetSequenceFileList(const std::string& directoryPath)
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

    bool CameraSequenceIO::Save(const std::string& filePath, const CameraSequenceAsset& asset)
    {
        json root;
        // 書き出す内容は常に現行フォーマット。読み込み時のバージョンを持ち回さない。
        root["version"] = CameraSequenceAsset::kCurrentVersion;
        root["timelineLength"] = asset.timelineLength;
        root["easingTypeIndex"] = asset.easingTypeIndex;
        root["shotsEnabled"] = asset.shotsEnabled;

        json keyframesJson = json::array();
        for (const auto& key : asset.keyframes) {
            json keyJson;
            keyJson["time"] = key.time;
            keyJson["snapshot"] = SnapshotToJson(key.snapshot);
            keyJson["label"] = key.label;
            keyJson["easingTypeIndex"] = key.easingTypeIndex;
            keyJson["interpolation"] = static_cast<int>(key.interpolation);
            keyJson["aimMode"] = static_cast<int>(key.aimMode);
            keyJson["aimPoint"] = JsonManager::Vector3ToJson(key.aimPoint);
            keyJson["aimObjectName"] = key.aimObjectName;
            keyJson["aimOffset"] = JsonManager::Vector3ToJson(key.aimOffset);
            keyJson["aimRoll"] = key.aimRoll;
            keyframesJson.push_back(keyJson);
        }
        root["keyframes"] = keyframesJson;

        json shotsJson = json::array();
        for (const auto& shot : asset.shots) {
            shotsJson.push_back(ShotToJson(shot));
        }
        root["shots"] = shotsJson;

        json eventsJson = json::array();
        for (const auto& event : asset.events) {
            eventsJson.push_back(EventToJson(event));
        }
        root["events"] = eventsJson;

        return JsonManager::GetInstance().SaveJson(filePath, root);
    }

    bool CameraSequenceIO::Load(const std::string& filePath, CameraSequenceAsset& outAsset)
    {
        // 欠けたキーは SafeGet の既定値で補う。古いバージョンのファイルも読めるようにするため
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

        if (root.contains("keyframes") && root["keyframes"].is_array()) {
            for (const auto& keyJson : root["keyframes"]) {
                CameraSequenceKeyframe key{};
                key.time = JsonManager::SafeGet(keyJson, "time", 0.0f);
                if (keyJson.contains("snapshot")) {
                    key.snapshot = JsonToSnapshot(keyJson["snapshot"]);
                }

                // 区間ごとの指定はバージョン 2.1 から。持っていないファイルは
                // 「シーケンス既定の緩急・直線」になり、従来どおりの見た目になる。
                key.label = JsonManager::SafeGet(keyJson, "label", std::string());
                key.easingTypeIndex = JsonManager::SafeGet(keyJson, "easingTypeIndex", kUseSequenceEasing);
                key.interpolation = ToInterpolation(
                    JsonManager::SafeGet(keyJson, "interpolation",
                        static_cast<int>(CameraSequenceInterpolation::Linear)));

                // 注視の指定はバージョン 2.2 から。持っていないファイルは Euler になり、
                // 保存された回転がそのまま使われる（従来どおりの見た目）。
                key.aimMode = ToAimMode(JsonManager::SafeGet(keyJson, "aimMode",
                    static_cast<int>(CameraSequenceAimMode::Euler)));
                key.aimPoint = JsonManager::SafeGetVector3(keyJson, "aimPoint");
                key.aimObjectName = JsonManager::SafeGet(keyJson, "aimObjectName", std::string());
                key.aimOffset = JsonManager::SafeGetVector3(keyJson, "aimOffset");
                key.aimRoll = JsonManager::SafeGet(keyJson, "aimRoll", 0.0f);

                asset.keyframes.push_back(key);
            }
        }

        if (root.contains("shots") && root["shots"].is_array()) {
            for (const auto& shotJson : root["shots"]) {
                asset.shots.push_back(JsonToShot(shotJson));
            }
        }

        if (root.contains("events") && root["events"].is_array()) {
            for (const auto& eventJson : root["events"]) {
                asset.events.push_back(JsonToEvent(eventJson));
            }
        }

        // 時刻の並びと範囲はここで整えておく。読み手が毎回気にしなくて済む。
        asset.Sanitize();
        asset.SortKeyframes();
        asset.SortEvents();

        outAsset = std::move(asset);
        return !outAsset.keyframes.empty();
    }
}
