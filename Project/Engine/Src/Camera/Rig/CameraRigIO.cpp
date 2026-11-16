#include "pch.h"
#include "CameraRigIO.h"

#include "Utility/JsonManager/JsonFields.h"
#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        // 保存された番号として読める範囲（0〜最後の値）
        constexpr int kBodyModeCount = static_cast<int>(CameraRigBodyMode::Rail) + 1;
        constexpr int kAimModeCount = static_cast<int>(CameraRigAimMode::FrameTargets) + 1;
        constexpr int kLensModeCount = static_cast<int>(CameraRigLensMode::SpeedToFov) + 1;
        constexpr int kOffsetSpaceCount = static_cast<int>(CameraRigOffsetSpace::Target) + 1;
        constexpr int kDistanceSourceCount = static_cast<int>(CameraRigDistanceSource::CameraToAim) + 1;
        constexpr int kDampingModeCount = static_cast<int>(CameraRigDampingMode::Spring) + 1;

        // ベクトルの項目は欄があるときだけ配列として読む（kIfPresent）

        /// @brief 対象参照の項目
        constexpr auto kTargetFields = [](auto& target, auto&& field) {
            field("objectName", target.objectName);
            field("offset", target.offset, JsonFields::kIfPresent);
            field("weight", target.weight);
        };

        /// @brief 軸ごとの追従の項目
        constexpr auto kFollowFields = [](auto& follow, auto&& field) {
            field("weight", follow.weight, JsonFields::kIfPresent);
            field("anchor", follow.anchor, JsonFields::kIfPresent);
        };

        /// @brief 居場所の項目（主対象・対象の並び・寄せる位置・レールの制御点は別に読み書きする）
        constexpr auto kBodyFields = [](auto& body, auto&& field) {
            field("mode", body.mode, kBodyModeCount, CameraRigBodyMode::FollowTarget);
            field("position", body.position, JsonFields::kIfPresent);
            field("rotation", body.rotation, JsonFields::kIfPresent);
            field("offset", body.offset, JsonFields::kIfPresent);
            field("offsetSpace", body.offsetSpace, kOffsetSpaceCount, CameraRigOffsetSpace::World);
            field("orbitDistance", body.orbitDistance);
            field("orbitYaw", body.orbitYaw);
            field("orbitPitch", body.orbitPitch);
            field("framePullBackPerMeter", body.framePullBackPerMeter);
            field("framePullBackMax", body.framePullBackMax);
            field("railLoop", body.railLoop);
            field("railFollowTarget", body.railFollowTarget);
            field("railPosition", body.railPosition);
            field("railOffset", body.railOffset, JsonFields::kIfPresent);
        };

        /// @brief 向きの項目（主対象・対象の並びは別に読み書きする）
        constexpr auto kAimFields = [](auto& aim, auto&& field) {
            field("mode", aim.mode, kAimModeCount, CameraRigAimMode::FollowBody);
            field("screenX", aim.screenX);
            field("screenY", aim.screenY);
            field("roll", aim.roll);
        };

        /// @brief 視野角の項目
        constexpr auto kLensFields = [](auto& lens, auto&& field) {
            field("mode", lens.mode, kLensModeCount, CameraRigLensMode::Fixed);
            field("fovDegrees", lens.fovDegrees);
            field("inputMin", lens.inputMin);
            field("inputMax", lens.inputMax);
            field("fovMinDegrees", lens.fovMinDegrees);
            field("fovMaxDegrees", lens.fovMaxDegrees);
            field("distanceSource", lens.distanceSource, kDistanceSourceCount, CameraRigDistanceSource::TargetSpread);
        };

        /// @brief 減衰の項目
        constexpr auto kDampingFields = [](auto& damping, auto&& field) {
            field("position", damping.position);
            field("rotation", damping.rotation);
            field("fov", damping.fov);
            field("aim", damping.aim);
            field("mode", damping.mode, kDampingModeCount, CameraRigDampingMode::Exponential);
        };

        /// @brief 対象参照の配列を JSON へ
        json TargetsToJson(const std::vector<CameraRigTargetRef>& targets)
        {
            json array = json::array();
            for (const auto& target : targets) {
                array.push_back(JsonFields::ToJsonObject(target, kTargetFields));
            }
            return array;
        }

        /// @brief JSON から対象参照の配列へ
        std::vector<CameraRigTargetRef> JsonToTargets(const json& parent, const char* key)
        {
            std::vector<CameraRigTargetRef> targets;
            if (!parent.contains(key) || !parent[key].is_array()) {
                return targets;
            }
            for (const auto& element : parent[key]) {
                targets.push_back(JsonFields::FromJsonObject<CameraRigTargetRef>(element, kTargetFields));
            }
            return targets;
        }

        json BodyToJson(const CameraRigBody& body)
        {
            json jsonData = JsonFields::ToJsonObject(body, kBodyFields);
            jsonData["target"] = JsonFields::ToJsonObject(body.target, kTargetFields);
            jsonData["targets"] = TargetsToJson(body.targets);
            jsonData["frameBias"] = JsonManager::Vector3ToJson(body.frameBias);

            json railPoints = json::array();
            for (const auto& point : body.railPoints) {
                railPoints.push_back(JsonManager::Vector3ToJson(point));
            }
            jsonData["railPoints"] = railPoints;
            return jsonData;
        }

        CameraRigBody JsonToBody(const json& jsonData)
        {
            // 既定値は型の初期値をそのまま使う。項目を後から足しても古いファイルが読める。
            CameraRigBody body = JsonFields::FromJsonObject<CameraRigBody>(jsonData, kBodyFields);

            if (jsonData.contains("target")) {
                body.target = JsonFields::FromJsonObject<CameraRigTargetRef>(jsonData["target"], kTargetFields);
            }
            body.targets = JsonToTargets(jsonData, "targets");
            if (jsonData.contains("frameBias")) {
                // 軸ごとにする前は 1 つの数値だった。古いファイルは全軸へ配る。
                const auto& biasJson = jsonData["frameBias"];
                if (biasJson.is_number()) {
                    const float bias = biasJson.get<float>();
                    body.frameBias = { bias, bias, bias };
                } else {
                    body.frameBias = JsonManager::JsonToVector3(biasJson);
                }
            }

            if (jsonData.contains("railPoints") && jsonData["railPoints"].is_array()) {
                for (const auto& element : jsonData["railPoints"]) {
                    body.railPoints.push_back(JsonManager::JsonToVector3(element));
                }
            }
            return body;
        }

        json AimToJson(const CameraRigAim& aim)
        {
            json jsonData = JsonFields::ToJsonObject(aim, kAimFields);
            jsonData["target"] = JsonFields::ToJsonObject(aim.target, kTargetFields);
            jsonData["targets"] = TargetsToJson(aim.targets);
            return jsonData;
        }

        CameraRigAim JsonToAim(const json& jsonData)
        {
            CameraRigAim aim = JsonFields::FromJsonObject<CameraRigAim>(jsonData, kAimFields);
            if (jsonData.contains("target")) {
                aim.target = JsonFields::FromJsonObject<CameraRigTargetRef>(jsonData["target"], kTargetFields);
            }
            aim.targets = JsonToTargets(jsonData, "targets");
            return aim;
        }
    }

    std::vector<std::string> CameraRigIO::GetRigFileList(const std::string& directoryPath)
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

    bool CameraRigIO::Save(const std::string& filePath, const CameraRigAsset& asset)
    {
        json root;
        // 書き出す内容は常に現行フォーマット。読み込み時のバージョンを持ち回さない。
        root["version"] = CameraRigAsset::kCurrentVersion;
        root["name"] = asset.name;
        root["body"] = BodyToJson(asset.body);
        root["aim"] = AimToJson(asset.aim);
        root["lens"] = JsonFields::ToJsonObject(asset.lens, kLensFields);
        root["damping"] = JsonFields::ToJsonObject(asset.damping, kDampingFields);
        root["follow"] = JsonFields::ToJsonObject(asset.follow, kFollowFields);

        return JsonManager::GetInstance().SaveJson(filePath, root);
    }

    bool CameraRigIO::Load(const std::string& filePath, CameraRigAsset& outAsset)
    {
        if (!JsonManager::GetInstance().FileExists(filePath)) {
            return false;
        }

        const json root = JsonManager::GetInstance().LoadJson(filePath);
        if (root.empty()) {
            return false;
        }

        CameraRigAsset asset{};
        asset.version = JsonManager::SafeGet(root, "version", std::string("1.0"));
        asset.name = JsonManager::SafeGet(root, "name", std::string{});

        if (root.contains("body")) {
            asset.body = JsonToBody(root["body"]);
        }
        if (root.contains("aim")) {
            asset.aim = JsonToAim(root["aim"]);
        }
        if (root.contains("lens")) {
            asset.lens = JsonFields::FromJsonObject<CameraRigLens>(root["lens"], kLensFields);
        }
        if (root.contains("damping")) {
            asset.damping = JsonFields::FromJsonObject<CameraRigDamping>(root["damping"], kDampingFields);
        }
        // 項目を持たない古いファイルは既定値（全軸そのまま追う）のままになる。
        if (root.contains("follow")) {
            asset.follow = JsonFields::FromJsonObject<CameraRigFollowAxes>(root["follow"], kFollowFields);
        }

        // 手で編集されたファイルでも必ず評価できる状態にしてから返す。
        asset.Sanitize();

        outAsset = std::move(asset);
        return true;
    }
}
