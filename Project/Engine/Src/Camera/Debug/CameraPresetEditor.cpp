#include "CameraPresetEditor.h"

#ifdef _DEBUG

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <filesystem>

#include "Camera/CameraPresetManager.h"
#include "Camera/ICamera.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    void CameraPresetEditor::DrawUI(CameraPresetManager& presetManager, ICamera* camera)
    {
        if (!camera) {
            return;
        }

        if (ImGui::CollapsingHeader("プリセット管理", ImGuiTreeNodeFlags_DefaultOpen)) {
            const std::string currentPresetPath = presetManager.GetCurrentPresetPath();
            if (!currentPresetPath.empty()) {
                const std::string presetName = GetPresetDisplayName(currentPresetPath);
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "現在のプリセット: %s", presetName.c_str());

                if (ImGui::Button("上書き保存 (Ctrl+S)", ImVec2(200, 0))) {
                    if (presetManager.SaveCurrentPreset(camera)) {
                        ImGui::OpenPopup("上書き保存成功");
                    } else {
                        ImGui::OpenPopup("上書き保存失敗");
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("プリセットをクリア", ImVec2(150, 0))) {
                    presetManager.ClearCurrentPreset();
                }

                ImGui::Separator();
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.0f, 1.0f), "現在のプリセット: なし");
                ImGui::Separator();
            }

            if (ImGui::BeginPopupModal("上書き保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットを上書き保存しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("上書き保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットの上書き保存に失敗しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();

            const std::string currentDirectory = presetManager.GetPresetDirectoryPath();
            if (currentDirectory.size() < sizeof(directoryPathBuffer_)) {
                memcpy(directoryPathBuffer_, currentDirectory.c_str(), currentDirectory.size() + 1);
            }

            ImGui::Text("保存先ディレクトリ");
            if (ImGui::InputText("##Directory", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
                presetManager.SetPresetDirectoryPath(directoryPathBuffer_);
            }

            ImGui::Separator();
            ImGui::Text("=== 保存 ===");
            ImGui::InputText("ファイル名", saveFileNameBuffer_, sizeof(saveFileNameBuffer_));

            if (ImGui::Button("プリセットを保存", ImVec2(200, 0))) {
                std::string fileName = saveFileNameBuffer_;
                if (!fileName.empty()) {
                    if (fileName.find(".json") == std::string::npos) {
                        fileName += ".json";
                    }

                    JsonManager::GetInstance().CreateJsonDirectory(presetManager.GetPresetDirectoryPath());
                    const std::string fullPath = presetManager.BuildPresetFilePath(fileName);
                    if (presetManager.SavePreset(camera, fullPath)) {
                        ImGui::OpenPopup("保存成功");
                    } else {
                        ImGui::OpenPopup("保存失敗");
                    }
                }
            }

            if (ImGui::BeginPopupModal("保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットを保存しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットの保存に失敗しました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Separator();
            ImGui::Text("=== 読み込み ===");

            if (ImGui::Button("リストを更新")) {
                presetManager.RefreshPresetFileList();
            }

            const auto& presetFileList = presetManager.GetCurrentPresetList();
            if (presetFileList.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "プリセットがありません");
            } else {
                selectedPresetIndex_ = std::clamp(selectedPresetIndex_, -1, static_cast<int>(presetFileList.size()) - 1);

                ImGui::BeginChild("PresetFileList", ImVec2(0, 150), true);
                for (int i = 0; i < static_cast<int>(presetFileList.size()); ++i) {
                    const bool isSelected = (i == selectedPresetIndex_);
                    if (ImGui::Selectable(presetFileList[i].c_str(), isSelected)) {
                        selectedPresetIndex_ = i;
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        const std::string fullPath = presetManager.BuildPresetFilePath(presetFileList[i]);
                        if (presetManager.LoadPreset(camera, fullPath)) {
                            ImGui::OpenPopup("読み込み成功");
                        } else {
                            ImGui::OpenPopup("読み込み失敗");
                        }
                    }
                }
                ImGui::EndChild();

                if (selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(presetFileList.size())) {
                    if (ImGui::Button("選択したプリセットを読み込み", ImVec2(150, 0))) {
                        const std::string fullPath = presetManager.BuildPresetFilePath(presetFileList[selectedPresetIndex_]);
                        if (presetManager.LoadPreset(camera, fullPath)) {
                            ImGui::OpenPopup("読み込み成功");
                        } else {
                            ImGui::OpenPopup("読み込み失敗");
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("選択したプリセットを削除", ImVec2(150, 0))) {
                        ImGui::OpenPopup("削除確認");
                    }

                    if (ImGui::BeginPopupModal("削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("本当に削除しますか？");
                        ImGui::Text("ファイル: %s", presetFileList[selectedPresetIndex_].c_str());
                        ImGui::Separator();

                        if (ImGui::Button("削除", ImVec2(120, 0))) {
                            const std::string fullPath = presetManager.BuildPresetFilePath(presetFileList[selectedPresetIndex_]);
                            if (presetManager.DeletePreset(fullPath)) {
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

                    if (ImGui::BeginPopupModal("削除成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("プリセットを削除しました。");
                        if (ImGui::Button("OK", ImVec2(120, 0))) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginPopupModal("削除失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("プリセットの削除に失敗しました。");
                        if (ImGui::Button("OK", ImVec2(120, 0))) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            if (ImGui::BeginPopupModal("読み込み成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("プリセットを読み込みました。");
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

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

    std::string CameraPresetEditor::GetPresetDisplayName(const std::string& fullPath) const
    {
        const std::filesystem::path path(fullPath);
        return path.stem().string();
    }
}

#endif // _DEBUG
