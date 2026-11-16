#include "pch.h"
#include "CameraSequenceIO.h"

#include "Utility/JsonManager/JsonFields.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        // 保存された番号として読める範囲（0〜最後の値）
        constexpr int kInterpolationCount = static_cast<int>(CameraSequenceInterpolation::Smooth) + 1;
        constexpr int kAimModeCount = static_cast<int>(CameraSequenceAimMode::LookAtObject) + 1;
        constexpr int kEventTypeCount = static_cast<int>(CameraSequenceEventType::TimeScale) + 1;
        constexpr int kTransitionTypeCount = static_cast<int>(CameraSequenceTransitionType::Blend) + 1;

        /// @brief スナップショットの投影の項目（"parameters" の欄）
        constexpr auto kParameterFields = [](auto& parameters, auto&& field) {
            field("fov", parameters.fov);
            field("nearClip", parameters.nearClip);
            field("farClip", parameters.farClip);
            field("aspectRatio", parameters.aspectRatio);
        };

        /// @brief ショットの項目（名前は欄が無ければ「ショット」）
        constexpr auto kShotFields = [](auto& shot, auto&& field) {
            field("name", shot.name, JsonFields::IfMissing(std::string("ショット")));
            field("startTime", shot.startTime);
            field("endTime", shot.endTime);
            field("enabled", shot.enabled);
            field("transitionType", shot.transitionType, kTransitionTypeCount, CameraSequenceTransitionType::Cut);
            field("blendDuration", shot.blendDuration);
        };

        /// @brief イベントの項目
        constexpr auto kEventFields = [](auto& event, auto&& field) {
            field("time", event.time);
            field("type", event.type, kEventTypeCount, CameraSequenceEventType::Callback);
            field("enabled", event.enabled);
            field("name", event.name);
            field("value", event.value);
            field("duration", event.duration);
        };

        /// @brief キーフレームの項目（スナップショットは別に読み書きする）
        constexpr auto kKeyframeFields = [](auto& key, auto&& field) {
            field("time", key.time);
            field("label", key.label);
            field("easingTypeIndex", key.easingTypeIndex);
            field("interpolation", key.interpolation, kInterpolationCount, CameraSequenceInterpolation::Linear);
            field("aimMode", key.aimMode, kAimModeCount, CameraSequenceAimMode::Euler);
            field("aimPoint", key.aimPoint);
            field("aimObjectName", key.aimObjectName);
            field("aimOffset", key.aimOffset);
            field("aimRoll", key.aimRoll);
        };

        /// @brief シーケンス全体の項目（版とキー・ショット・イベントの並びは別に読み書きする）
        constexpr auto kAssetFields = [](auto& asset, auto&& field) {
            field("timelineLength", asset.timelineLength);
            field("easingTypeIndex", asset.easingTypeIndex);
            field("shotsEnabled", asset.shotsEnabled);
        };

        /// @brief スナップショットを JSON へ変換
        json SnapshotToJson(const CameraSnapshot& snapshot)
        {
            // カメラが 1 種類になったため、スナップショットは常に Transform + 投影パラメータ。
            json jsonData;
            jsonData["position"] = JsonManager::Vector3ToJson(snapshot.position);
            jsonData["rotation"] = JsonManager::Vector3ToJson(snapshot.rotation);
            jsonData["scale"] = JsonManager::Vector3ToJson(snapshot.scale);

            json paramsJson;
            JsonFields::WriteFields(paramsJson, snapshot.parameters, kParameterFields);
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
                JsonFields::ReadFields(jsonData["parameters"], snapshot.parameters, kParameterFields);
            }

            return snapshot;
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
        JsonFields::WriteFields(root, asset, kAssetFields);

        json keyframesJson = json::array();
        for (const auto& key : asset.keyframes) {
            json keyJson = JsonFields::ToJsonObject(key, kKeyframeFields);
            keyJson["snapshot"] = SnapshotToJson(key.snapshot);
            keyframesJson.push_back(keyJson);
        }
        root["keyframes"] = keyframesJson;

        json shotsJson = json::array();
        for (const auto& shot : asset.shots) {
            shotsJson.push_back(JsonFields::ToJsonObject(shot, kShotFields));
        }
        root["shots"] = shotsJson;

        json eventsJson = json::array();
        for (const auto& event : asset.events) {
            eventsJson.push_back(JsonFields::ToJsonObject(event, kEventFields));
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
        JsonFields::ReadFields(root, asset, kAssetFields);

        if (root.contains("keyframes") && root["keyframes"].is_array()) {
            for (const auto& keyJson : root["keyframes"]) {
                CameraSequenceKeyframe key = JsonFields::FromJsonObject<CameraSequenceKeyframe>(keyJson, kKeyframeFields);
                if (keyJson.contains("snapshot")) {
                    key.snapshot = JsonToSnapshot(keyJson["snapshot"]);
                }
                asset.keyframes.push_back(key);
            }
        }

        if (root.contains("shots") && root["shots"].is_array()) {
            for (const auto& shotJson : root["shots"]) {
                asset.shots.push_back(JsonFields::FromJsonObject<CameraSequenceShot>(shotJson, kShotFields));
            }
        }

        if (root.contains("events") && root["events"].is_array()) {
            for (const auto& eventJson : root["events"]) {
                asset.events.push_back(JsonFields::FromJsonObject<CameraSequenceEvent>(eventJson, kEventFields));
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
