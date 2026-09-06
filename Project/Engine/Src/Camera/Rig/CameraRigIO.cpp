#include "pch.h"
#include "CameraRigIO.h"

#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief 対象参照を JSON へ
        json TargetToJson(const CameraRigTargetRef& target)
        {
            json jsonData;
            jsonData["objectName"] = target.objectName;
            jsonData["offset"] = JsonManager::Vector3ToJson(target.offset);
            jsonData["weight"] = target.weight;
            return jsonData;
        }

        /// @brief JSON から対象参照へ
        CameraRigTargetRef JsonToTarget(const json& jsonData)
        {
            CameraRigTargetRef target{};
            target.objectName = JsonManager::SafeGet(jsonData, "objectName", std::string{});
            if (jsonData.contains("offset")) {
                target.offset = JsonManager::JsonToVector3(jsonData["offset"]);
            }
            target.weight = JsonManager::SafeGet(jsonData, "weight", 1.0f);
            return target;
        }

        /// @brief 対象参照の配列を JSON へ
        json TargetsToJson(const std::vector<CameraRigTargetRef>& targets)
        {
            json array = json::array();
            for (const auto& target : targets) {
                array.push_back(TargetToJson(target));
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
                targets.push_back(JsonToTarget(element));
            }
            return targets;
        }

        /// @brief 保存された番号を Body のモードへ戻す（未知の値は FollowTarget 扱い）
        CameraRigBodyMode ToBodyMode(int value)
        {
            switch (value) {
            case static_cast<int>(CameraRigBodyMode::Fixed):        return CameraRigBodyMode::Fixed;
            case static_cast<int>(CameraRigBodyMode::OrbitTarget):  return CameraRigBodyMode::OrbitTarget;
            case static_cast<int>(CameraRigBodyMode::FrameTargets): return CameraRigBodyMode::FrameTargets;
            case static_cast<int>(CameraRigBodyMode::Rail):         return CameraRigBodyMode::Rail;
            default:                                                return CameraRigBodyMode::FollowTarget;
            }
        }

        /// @brief 保存された番号を Aim のモードへ戻す（未知の値は FollowBody 扱い）
        /// @details 未知の値で LookAt 系へ落とすと、対象が空のまま評価が失敗し続ける。
        ///          対象を要らない FollowBody なら、少なくとも画は出る。
        CameraRigAimMode ToAimMode(int value)
        {
            switch (value) {
            case static_cast<int>(CameraRigAimMode::LookAtTarget): return CameraRigAimMode::LookAtTarget;
            case static_cast<int>(CameraRigAimMode::FrameTargets): return CameraRigAimMode::FrameTargets;
            default:                                               return CameraRigAimMode::FollowBody;
            }
        }

        /// @brief 保存された番号を Lens のモードへ戻す（未知の値は Fixed 扱い）
        CameraRigLensMode ToLensMode(int value)
        {
            switch (value) {
            case static_cast<int>(CameraRigLensMode::DistanceToFov): return CameraRigLensMode::DistanceToFov;
            case static_cast<int>(CameraRigLensMode::SpeedToFov):    return CameraRigLensMode::SpeedToFov;
            default:                                                 return CameraRigLensMode::Fixed;
            }
        }

        /// @brief 保存された番号をオフセットの解釈へ戻す（未知の値は World 扱い）
        CameraRigOffsetSpace ToOffsetSpace(int value)
        {
            return (value == static_cast<int>(CameraRigOffsetSpace::Target))
                ? CameraRigOffsetSpace::Target
                : CameraRigOffsetSpace::World;
        }

        /// @brief 保存された番号を距離の種類へ戻す（未知の値は TargetSpread 扱い）
        CameraRigDistanceSource ToDistanceSource(int value)
        {
            return (value == static_cast<int>(CameraRigDistanceSource::CameraToAim))
                ? CameraRigDistanceSource::CameraToAim
                : CameraRigDistanceSource::TargetSpread;
        }

        /// @brief 保存された番号を寄せ方へ戻す（未知の値は Exponential 扱い）
        /// @details 指定を持たない古いファイルもここを通って従来の挙動になる。
        CameraRigDampingMode ToDampingMode(int value)
        {
            return (value == static_cast<int>(CameraRigDampingMode::Spring))
                ? CameraRigDampingMode::Spring
                : CameraRigDampingMode::Exponential;
        }

        json BodyToJson(const CameraRigBody& body)
        {
            json jsonData;
            jsonData["mode"] = static_cast<int>(body.mode);
            jsonData["position"] = JsonManager::Vector3ToJson(body.position);
            jsonData["rotation"] = JsonManager::Vector3ToJson(body.rotation);
            jsonData["target"] = TargetToJson(body.target);
            jsonData["offset"] = JsonManager::Vector3ToJson(body.offset);
            jsonData["offsetSpace"] = static_cast<int>(body.offsetSpace);
            jsonData["orbitDistance"] = body.orbitDistance;
            jsonData["orbitYaw"] = body.orbitYaw;
            jsonData["orbitPitch"] = body.orbitPitch;
            jsonData["targets"] = TargetsToJson(body.targets);
            jsonData["frameBias"] = JsonManager::Vector3ToJson(body.frameBias);
            jsonData["framePullBackPerMeter"] = body.framePullBackPerMeter;
            jsonData["framePullBackMax"] = body.framePullBackMax;

            json railPoints = json::array();
            for (const auto& point : body.railPoints) {
                railPoints.push_back(JsonManager::Vector3ToJson(point));
            }
            jsonData["railPoints"] = railPoints;
            jsonData["railLoop"] = body.railLoop;
            jsonData["railFollowTarget"] = body.railFollowTarget;
            jsonData["railPosition"] = body.railPosition;
            jsonData["railOffset"] = JsonManager::Vector3ToJson(body.railOffset);
            return jsonData;
        }

        CameraRigBody JsonToBody(const json& jsonData)
        {
            // 既定値は型の初期値をそのまま使う。項目を後から足しても古いファイルが読める。
            CameraRigBody body{};
            body.mode = ToBodyMode(JsonManager::SafeGet(jsonData, "mode",
                static_cast<int>(body.mode)));

            if (jsonData.contains("position")) {
                body.position = JsonManager::JsonToVector3(jsonData["position"]);
            }
            if (jsonData.contains("rotation")) {
                body.rotation = JsonManager::JsonToVector3(jsonData["rotation"]);
            }
            if (jsonData.contains("target")) {
                body.target = JsonToTarget(jsonData["target"]);
            }
            if (jsonData.contains("offset")) {
                body.offset = JsonManager::JsonToVector3(jsonData["offset"]);
            }
            body.offsetSpace = ToOffsetSpace(JsonManager::SafeGet(jsonData, "offsetSpace",
                static_cast<int>(body.offsetSpace)));

            body.orbitDistance = JsonManager::SafeGet(jsonData, "orbitDistance", body.orbitDistance);
            body.orbitYaw = JsonManager::SafeGet(jsonData, "orbitYaw", body.orbitYaw);
            body.orbitPitch = JsonManager::SafeGet(jsonData, "orbitPitch", body.orbitPitch);

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
            body.framePullBackPerMeter = JsonManager::SafeGet(jsonData,
                "framePullBackPerMeter", body.framePullBackPerMeter);
            body.framePullBackMax = JsonManager::SafeGet(jsonData,
                "framePullBackMax", body.framePullBackMax);

            if (jsonData.contains("railPoints") && jsonData["railPoints"].is_array()) {
                for (const auto& element : jsonData["railPoints"]) {
                    body.railPoints.push_back(JsonManager::JsonToVector3(element));
                }
            }
            body.railLoop = JsonManager::SafeGet(jsonData, "railLoop", body.railLoop);
            body.railFollowTarget = JsonManager::SafeGet(jsonData, "railFollowTarget",
                body.railFollowTarget);
            body.railPosition = JsonManager::SafeGet(jsonData, "railPosition", body.railPosition);
            if (jsonData.contains("railOffset")) {
                body.railOffset = JsonManager::JsonToVector3(jsonData["railOffset"]);
            }
            return body;
        }

        json AimToJson(const CameraRigAim& aim)
        {
            json jsonData;
            jsonData["mode"] = static_cast<int>(aim.mode);
            jsonData["target"] = TargetToJson(aim.target);
            jsonData["targets"] = TargetsToJson(aim.targets);
            jsonData["screenX"] = aim.screenX;
            jsonData["screenY"] = aim.screenY;
            jsonData["roll"] = aim.roll;
            return jsonData;
        }

        CameraRigAim JsonToAim(const json& jsonData)
        {
            CameraRigAim aim{};
            aim.mode = ToAimMode(JsonManager::SafeGet(jsonData, "mode", static_cast<int>(aim.mode)));
            if (jsonData.contains("target")) {
                aim.target = JsonToTarget(jsonData["target"]);
            }
            aim.targets = JsonToTargets(jsonData, "targets");
            aim.screenX = JsonManager::SafeGet(jsonData, "screenX", aim.screenX);
            aim.screenY = JsonManager::SafeGet(jsonData, "screenY", aim.screenY);
            aim.roll = JsonManager::SafeGet(jsonData, "roll", aim.roll);
            return aim;
        }

        json LensToJson(const CameraRigLens& lens)
        {
            json jsonData;
            jsonData["mode"] = static_cast<int>(lens.mode);
            jsonData["fovDegrees"] = lens.fovDegrees;
            jsonData["inputMin"] = lens.inputMin;
            jsonData["inputMax"] = lens.inputMax;
            jsonData["fovMinDegrees"] = lens.fovMinDegrees;
            jsonData["fovMaxDegrees"] = lens.fovMaxDegrees;
            jsonData["distanceSource"] = static_cast<int>(lens.distanceSource);
            return jsonData;
        }

        CameraRigLens JsonToLens(const json& jsonData)
        {
            CameraRigLens lens{};
            lens.mode = ToLensMode(JsonManager::SafeGet(jsonData, "mode",
                static_cast<int>(lens.mode)));
            lens.fovDegrees = JsonManager::SafeGet(jsonData, "fovDegrees", lens.fovDegrees);
            lens.inputMin = JsonManager::SafeGet(jsonData, "inputMin", lens.inputMin);
            lens.inputMax = JsonManager::SafeGet(jsonData, "inputMax", lens.inputMax);
            lens.fovMinDegrees = JsonManager::SafeGet(jsonData, "fovMinDegrees", lens.fovMinDegrees);
            lens.fovMaxDegrees = JsonManager::SafeGet(jsonData, "fovMaxDegrees", lens.fovMaxDegrees);
            lens.distanceSource = ToDistanceSource(JsonManager::SafeGet(jsonData,
                "distanceSource", static_cast<int>(lens.distanceSource)));
            return lens;
        }

        json DampingToJson(const CameraRigDamping& damping)
        {
            json jsonData;
            jsonData["position"] = damping.position;
            jsonData["rotation"] = damping.rotation;
            jsonData["fov"] = damping.fov;
            jsonData["aim"] = damping.aim;
            jsonData["mode"] = static_cast<int>(damping.mode);
            return jsonData;
        }

        CameraRigDamping JsonToDamping(const json& jsonData)
        {
            CameraRigDamping damping{};
            damping.position = JsonManager::SafeGet(jsonData, "position", damping.position);
            damping.rotation = JsonManager::SafeGet(jsonData, "rotation", damping.rotation);
            damping.fov = JsonManager::SafeGet(jsonData, "fov", damping.fov);
            damping.aim = JsonManager::SafeGet(jsonData, "aim", damping.aim);
            damping.mode = ToDampingMode(JsonManager::SafeGet(jsonData, "mode",
                static_cast<int>(damping.mode)));
            return damping;
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
        root["lens"] = LensToJson(asset.lens);
        root["damping"] = DampingToJson(asset.damping);

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
            asset.lens = JsonToLens(root["lens"]);
        }
        if (root.contains("damping")) {
            asset.damping = JsonToDamping(root["damping"]);
        }

        // 手で編集されたファイルでも必ず評価できる状態にしてから返す。
        asset.Sanitize();

        outAsset = std::move(asset);
        return true;
    }
}
