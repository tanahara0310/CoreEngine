#include "CameraPresetManager.h"

#ifdef _DEBUG
#include <imgui.h>
#endif

#include <filesystem>
#include <iostream>
#include "Engine/Camera/ICamera.h"
#include "Engine/Camera/Debug/DebugCamera.h"
#include "Engine/Camera/Release/Camera.h"

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
            currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());
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

        // ファイルが存在するかチェック
        if (!JsonManager::GetInstance().FileExists(filePath)) {
            std::cerr << "Camera preset file not found: " << filePath << std::endl;
            return false;
        }

        // ファイルを読み込み
        json presetData = JsonManager::GetInstance().LoadJson(filePath);
        if (presetData.empty()) {
            std::cerr << "Failed to load camera preset: " << filePath << std::endl;
            return false;
        }

        // スナップショットに変換
        CameraSnapshot snapshot = JsonToSnapshot(presetData);

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
        currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());

        return true;
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
        presetFileList_ = GetPresetList(directoryPathBuffer_);
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

    json CameraPresetManager::SnapshotToJson(const CameraSnapshot& snapshot)
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

    CameraSnapshot CameraPresetManager::JsonToSnapshot(const json& jsonData)
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
                    currentPresetName_.clear();
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

#ifdef _DEBUG

    void CameraPresetManager::ShowImGui(ICamera* camera)
    {
        if (!camera) {
            return;
        }

        if (ImGui::CollapsingHeader("プリセット管理", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 現在読み込まれているプリセット表示
            if (!currentPresetPath_.empty()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "現在のプリセット: %s", currentPresetName_.c_str());

                // 上書き保存ボタン
                if (ImGui::Button("上書き保存 (Ctrl+S)", ImVec2(200, 0))) {
                    if (SaveCurrentPreset(camera)) {
                        ImGui::OpenPopup("上書き保存成功");
                    } else {
                        ImGui::OpenPopup("上書き保存失敗");
                    }
                }

                ImGui::SameLine();

                // プリセットをクリア
                if (ImGui::Button("プリセットをクリア", ImVec2(150, 0))) {
                    currentPresetPath_.clear();
                    currentPresetName_.clear();
                }

                ImGui::Separator();
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.0f, 1.0f), "現在のプリセット: なし");
                ImGui::Separator();
            }

            // 上書き保存成功ポップアップ
            if (ImGui::BeginPopupModal("上書き保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットを上書き保存しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // 上書き保存失敗ポップアップ
            if (ImGui::BeginPopupModal("上書き保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットの上書き保存に失敗しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            // ディレクトリパス設定
            ImGui::Text("保存先ディレクトリ");
            if (ImGui::InputText("##Directory", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
                needUpdateFileList_ = true;
            }

            ImGui::Separator();

            // 保存セクション
            ImGui::Text("=== 保存 ===");
            ImGui::InputText("ファイル名", saveFileNameBuffer_, sizeof(saveFileNameBuffer_));

            if (ImGui::Button("プリセットを保存", ImVec2(200, 0))) {
                std::string fileName = std::string(saveFileNameBuffer_);
                if (!fileName.empty()) {
                    // 拡張子がない場合は追加
                    if (fileName.find(".json") == std::string::npos) {
                        fileName += ".json";
                    }

                std::string fullPath = std::string(directoryPathBuffer_) + fileName;

                // ディレクトリが存在しない場合は作成
                JsonManager::GetInstance().CreateJsonDirectory(directoryPathBuffer_);

                if (SavePreset(camera, fullPath)) {
                        ImGui::OpenPopup("保存成功");
                    } else {
                        ImGui::OpenPopup("保存失敗");
                    }
                }
            }

            // 保存成功ポップアップ
            if (ImGui::BeginPopupModal("保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットを保存しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // 保存失敗ポップアップ
            if (ImGui::BeginPopupModal("保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットの保存に失敗しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            // 読み込みセクション
            ImGui::Text("=== 読み込み ===");

            // ファイルリスト更新ボタン
            if (ImGui::Button("リストを更新") || needUpdateFileList_) {
                UpdatePresetFileList();
            }

            // プリセットファイルリスト表示
            if (presetFileList_.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "プリセットがありません");
            } else {
                ImGui::BeginChild("PresetFileList", ImVec2(0, 150), true);

                for (int i = 0; i < static_cast<int>(presetFileList_.size()); i++) {
                    bool isSelected = (i == selectedPresetIndex_);

                    if (ImGui::Selectable(presetFileList_[i].c_str(), isSelected)) {
                        selectedPresetIndex_ = i;
                    }

                    // ダブルクリックで読み込み
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        std::string fullPath = std::string(directoryPathBuffer_) + presetFileList_[i];
                        if (LoadPreset(camera, fullPath)) {
                            ImGui::OpenPopup("読み込み成功");
                        } else {
                            ImGui::OpenPopup("読み込み失敗");
                        }
                    }
                }

                ImGui::EndChild();

            // 選択中のプリセット操作
            if (selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(presetFileList_.size())) {
                if (ImGui::Button("選択したプリセットを読み込み", ImVec2(150, 0))) {
                    std::string fullPath = std::string(directoryPathBuffer_) + presetFileList_[selectedPresetIndex_];
                    if (LoadPreset(camera, fullPath)) {
                        ImGui::OpenPopup("読み込み成功");
                    } else {
                        ImGui::OpenPopup("読み込み失敗");
                    }
                }

                ImGui::SameLine();

                if (ImGui::Button("選択したプリセットを削除", ImVec2(150, 0))) {
                    ImGui::OpenPopup("削除確認");
                }

                // 削除確認ポップアップ
                if (ImGui::BeginPopupModal("削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("本当に削除しますか？");
                    ImGui::Text("ファイル: %s", presetFileList_[selectedPresetIndex_].c_str());
                    ImGui::Separator();

                    if (ImGui::Button("削除", ImVec2(120, 0))) {
                        std::string fullPath = std::string(directoryPathBuffer_) + presetFileList_[selectedPresetIndex_];
                        if (DeletePreset(fullPath)) {
                            selectedPresetIndex_ = -1;
                            ImGui::CloseCurrentPopup();
                            ImGui::OpenPopup("削除成功");
                        } else {
                            ImGui::CloseCurrentPopup();
                            ImGui::OpenPopup("削除失敗");
                        }
                    }

                    ImGui::SameLine();

                    if (ImGui::Button("キャンセル", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                // 削除成功ポップアップ
                if (ImGui::BeginPopupModal("削除成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("プリセットを削除しました。");
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // 削除失敗ポップアップ
                if (ImGui::BeginPopupModal("削除失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("プリセットの削除に失敗しました。");
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        }

            // 読み込み成功ポップアップ
            if (ImGui::BeginPopupModal("読み込み成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットを読み込みました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // 読み込み失敗ポップアップ
            if (ImGui::BeginPopupModal("読み込み失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットの読み込みに失敗しました。");
                ImGui::Text("カメラタイプが一致しているか確認してください。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

#endif // _DEBUG

}
