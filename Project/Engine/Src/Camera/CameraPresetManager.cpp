#include "CameraPresetManager.h"

#include <filesystem>
#include <iostream>
#include "Camera/ICamera.h"
#include "Camera/Debug/DebugCamera.h"
#include "Camera/Release/Camera.h"

namespace CoreEngine
{

    bool CameraPresetManager::SavePreset(ICamera* camera, const std::string& filePath)
    {
        if (!camera) {
            return false;
        }

        // カメラタイプを判定してスナップショット作成
        CameraSnapshot snapshot;

        DebugCamera* debugCam = dynamic_cast<DebugCamera*>(camera);
        if (debugCam) {
            snapshot = debugCam->CaptureSnapshot("");
        } else {
            Camera* releaseCam = dynamic_cast<Camera*>(camera);
            if (releaseCam) {
                snapshot = releaseCam->CaptureSnapshot("");
            } else {
                return false;
            }
        }

        // スナップショットをJSONに変換
        json presetData = SnapshotToJson(snapshot);

        // メタデータ追加
        presetData["version"] = "1.0";

        // ファイルに保存
        bool success = JsonManager::GetInstance().SaveJson(filePath, presetData);
        if (success) {
            std::cout << "Camera preset saved: " << filePath << std::endl;
            needUpdateFileList_ = true;

            // 保存したファイルを現在のプリセットとして設定
            currentPresetPath_ = filePath;
        } else {
            std::cerr << "Failed to save camera preset: " << filePath << std::endl;
        }

        return success;
    }

    bool CameraPresetManager::LoadPreset(ICamera* camera, const std::string& filePath)
    {
        if (!camera) {
            return false;
        }

        // JSON読み込みとスナップショット変換は共通関数へ委譲する。
        CameraSnapshot snapshot;
        if (!LoadSnapshotFromFile(filePath, snapshot)) {
            return false;
        }

        // カメラタイプに応じて復元
        if (snapshot.isDebugCamera) {
            DebugCamera* debugCam = dynamic_cast<DebugCamera*>(camera);
            if (debugCam) {
                debugCam->RestoreSnapshot(snapshot);
            } else {
                std::cerr << "Camera type mismatch: preset is for DebugCamera" << std::endl;
                return false;
            }
        } else {
            Camera* releaseCam = dynamic_cast<Camera*>(camera);
            if (releaseCam) {
                releaseCam->RestoreSnapshot(snapshot);
            } else {
                std::cerr << "Camera type mismatch: preset is for ReleaseCamera" << std::endl;
                return false;
            }
        }

        std::cout << "Camera preset loaded: " << filePath << std::endl;

        // 読み込み成功したら現在のプリセットとして設定
        currentPresetPath_ = filePath;

        return true;
    }

    void CameraPresetManager::ClearCurrentPreset()
    {
        currentPresetPath_.clear();
    }

    void CameraPresetManager::SetPresetDirectoryPath(const std::string& directoryPath)
    {
        if (directoryPath == presetDirectoryPath_) {
            return;
        }

        presetDirectoryPath_ = directoryPath;
        needUpdateFileList_ = true;
    }

    void CameraPresetManager::RefreshPresetFileList()
    {
        UpdatePresetFileList();
    }

    bool CameraPresetManager::LoadSnapshotFromFile(const std::string& filePath, CameraSnapshot& outSnapshot) const
    {
        // ファイルが存在するかチェック
        if (!JsonManager::GetInstance().FileExists(filePath)) {
            std::cerr << "Camera preset file not found: " << filePath << std::endl;
            return false;
        }

        // ファイルを読み込んでJSON→Snapshotへ変換
        json presetData = JsonManager::GetInstance().LoadJson(filePath);
        if (presetData.empty()) {
            std::cerr << "Failed to load camera preset: " << filePath << std::endl;
            return false;
        }

        outSnapshot = JsonToSnapshot(presetData);
        return true;
    }

    std::string CameraPresetManager::BuildPresetFilePath(const std::string& fileName) const
    {
        // ディレクトリ連結を`std::filesystem::path`で行い、
        // 末尾スラッシュ有無によるパス崩れを防ぐ。
        const std::filesystem::path baseDir(presetDirectoryPath_);
        return (baseDir / fileName).string();
    }

    std::vector<std::string> CameraPresetManager::GetPresetList(const std::string& directory)
    {
        std::vector<std::string> fileList;

        if (!std::filesystem::exists(directory)) {
            return fileList;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                fileList.push_back(entry.path().filename().string());
            }
        }

        return fileList;
    }

    void CameraPresetManager::UpdatePresetFileList()
    {
        presetFileList_ = GetPresetList(presetDirectoryPath_);
        needUpdateFileList_ = false;
    }

    std::string CameraPresetManager::GetFileNameWithoutExtension(const std::string& filename)
    {
        size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos) {
            return filename.substr(0, dotPos);
        }
        return filename;
    }

    json CameraPresetManager::SnapshotToJson(const CameraSnapshot& snapshot) const
    {
        json jsonData;

        jsonData["isDebugCamera"] = snapshot.isDebugCamera;

        if (snapshot.isDebugCamera) {
            jsonData["target"] = JsonManager::Vector3ToJson(snapshot.target);
            jsonData["distance"] = snapshot.distance;
            jsonData["pitch"] = snapshot.pitch;
            jsonData["yaw"] = snapshot.yaw;
        } else {
            jsonData["position"] = JsonManager::Vector3ToJson(snapshot.position);
            jsonData["rotation"] = JsonManager::Vector3ToJson(snapshot.rotation);
            jsonData["scale"] = JsonManager::Vector3ToJson(snapshot.scale);
        }

        // カメラパラメータ
        json paramsJson;
        paramsJson["fov"] = snapshot.parameters.fov;
        paramsJson["nearClip"] = snapshot.parameters.nearClip;
        paramsJson["farClip"] = snapshot.parameters.farClip;
        paramsJson["aspectRatio"] = snapshot.parameters.aspectRatio;
        jsonData["parameters"] = paramsJson;

        return jsonData;
    }

    CameraSnapshot CameraPresetManager::JsonToSnapshot(const json& jsonData) const
    {
        CameraSnapshot snapshot;

        snapshot.isDebugCamera = JsonManager::SafeGet(jsonData, "isDebugCamera", false);

        if (snapshot.isDebugCamera) {
            snapshot.target = JsonManager::JsonToVector3(jsonData["target"]);
            snapshot.distance = JsonManager::SafeGet(jsonData, "distance", 20.0f);
            snapshot.pitch = JsonManager::SafeGet(jsonData, "pitch", 0.25f);
            snapshot.yaw = JsonManager::SafeGet(jsonData, "yaw", 3.14159265359f);
        } else {
            snapshot.position = JsonManager::JsonToVector3(jsonData["position"]);
            snapshot.rotation = JsonManager::JsonToVector3(jsonData["rotation"]);
            snapshot.scale = JsonManager::JsonToVector3(jsonData["scale"]);
        }

        // カメラパラメータ
        if (jsonData.contains("parameters")) {
            const auto& params = jsonData["parameters"];
            snapshot.parameters.fov = JsonManager::SafeGet(params, "fov", 0.45f);
            snapshot.parameters.nearClip = JsonManager::SafeGet(params, "nearClip", 0.1f);
            snapshot.parameters.farClip = JsonManager::SafeGet(params, "farClip", 1000.0f);
            snapshot.parameters.aspectRatio = JsonManager::SafeGet(params, "aspectRatio", 0.0f);
        }

        return snapshot;
    }

    bool CameraPresetManager::SaveCurrentPreset(ICamera* camera)
    {
        if (currentPresetPath_.empty()) {
            return false;
        }

        return SavePreset(camera, currentPresetPath_);
    }

    bool CameraPresetManager::DeletePreset(const std::string& filePath)
    {
        try {
            if (std::filesystem::exists(filePath)) {
                std::filesystem::remove(filePath);
                std::cout << "Camera preset deleted: " << filePath << std::endl;

                // 削除したファイルが現在のプリセットだった場合はクリア
                if (filePath == currentPresetPath_) {
                    currentPresetPath_.clear();
                }

                needUpdateFileList_ = true;
                return true;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error deleting preset: " << e.what() << std::endl;
        }
        return false;
    }

    const std::vector<std::string>& CameraPresetManager::GetCurrentPresetList()
    {
        if (needUpdateFileList_) {
            UpdatePresetFileList();
        }
        return presetFileList_;
    }

}
