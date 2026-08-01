#include "pch.h"
#include "PostEffectPresetManager.h"
#include "PostEffectManager.h"
#include "Utility/CVar/CVarSerialization.h"
#include <filesystem>
#include <iostream>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
namespace
{
    /// @brief プリセットが対象とする CVar の接頭辞
    /// @details ポストエフェクト以外の "r.*"（大気・雲など）を巻き込まないよう明示列挙する。
    ///          エフェクトを追加したらここにも接頭辞を足すこと（漏れてもプリセットに
    ///          含まれないだけで、前回状態の自動保存は CVars.json 側が全件行う）
    constexpr const char* kPostEffectCVarPrefixes[] = {
        "r.GrayScale", "r.Blur", "r.RadialBlur", "r.Shockwave", "r.Vignette",
        "r.ColorGrading", "r.ChromaticAberration", "r.Sepia", "r.Invert",
        "r.Random", "r.RasterScroll", "r.Fade", "r.Bloom", "r.LensFlare",
        "r.Outline", "r.Dissolve", "r.AutoExposure",
    };
}

json PostEffectPresetManager::CaptureToJson()
{
    json presetData;
    json cvars = json::object();
    for (const char* prefix : kPostEffectCVarPrefixes) {
        // プリセットは完全なスナップショットなのでデフォルト値も含めて保存する
        CVarSerialization::Save(cvars, prefix, /*skipDefaults=*/false);
    }
    presetData["cvars"] = cvars;
    presetData["version"] = "2.0";
    return presetData;
}

void PostEffectPresetManager::ApplyFromJson(const json& presetData)
{
    if (!presetData.contains("cvars")) {
        return;
    }
    const json& cvars = presetData["cvars"];
    for (const char* prefix : kPostEffectCVarPrefixes) {
        CVarSerialization::Load(cvars, prefix);
    }
}

bool PostEffectPresetManager::SavePreset(const PostEffectManager* postEffectManager, const std::string& filePath)
{
    (void)postEffectManager;  // 状態は CVar が持つため参照不要（呼び出し側の互換のため引数は残す）
    json presetData = CaptureToJson();

    bool success = JsonManager::GetInstance().SaveJson(filePath, presetData);
    if (success) {
        std::cout << "PostEffect preset saved: " << filePath << std::endl;
        needUpdateFileList_ = true;
        
        currentPresetPath_ = filePath;
        currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());
    } else {
        std::cerr << "Failed to save PostEffect preset: " << filePath << std::endl;
    }

    return success;
}

bool PostEffectPresetManager::LoadPreset(PostEffectManager* postEffectManager, const std::string& filePath)
{
    if (!JsonManager::GetInstance().FileExists(filePath)) {
        std::cerr << "PostEffect preset file not found: " << filePath << std::endl;
        return false;
    }

    json presetData = JsonManager::GetInstance().LoadJson(filePath);
    if (presetData.empty()) {
        std::cerr << "Failed to load PostEffect preset: " << filePath << std::endl;
        return false;
    }

    (void)postEffectManager;  // 状態は CVar が持つため参照不要
    ApplyFromJson(presetData);

    currentPresetPath_ = filePath;
    currentPresetName_ = GetFileNameWithoutExtension(std::filesystem::path(filePath).filename().string());

    std::cout << "PostEffect preset loaded: " << filePath << std::endl;
    return true;
}

std::vector<std::string> PostEffectPresetManager::GetPresetList(const std::string& directory)
{
    std::vector<std::string> fileList;

    try {
        if (!std::filesystem::exists(directory)) {
            return fileList;
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                fileList.push_back(entry.path().filename().string());
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error listing PostEffect preset files: " << e.what() << std::endl;
    }

    return fileList;
}

void PostEffectPresetManager::ShowImGui(PostEffectManager* postEffectManager)
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("プリセット管理")) {
        // キーボードショートカット: Ctrl+S で上書き保存
        if (!currentPresetPath_.empty() && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (SaveCurrentPreset(postEffectManager)) {
                ImGui::OpenPopup("上書き保存成功");
            } else {
                ImGui::OpenPopup("上書き保存失敗");
            }
        }

        // 現在読み込まれているプリセット情報を表示
        if (!currentPresetPath_.empty()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "現在のプリセット: %s", currentPresetName_.c_str());
            ImGui::Text("パス: %s", currentPresetPath_.c_str());
            
            // 上書き保存ボタン
            if (ImGui::Button("上書き保存 (Ctrl+S)", ImVec2(200, 0))) {
                if (SaveCurrentPreset(postEffectManager)) {
                    ImGui::OpenPopup("上書き保存成功");
                } else {
                    ImGui::OpenPopup("上書き保存失敗");
                }
            }
            
            UI::SameLine();
            
            // プリセットをクリア
            if (ImGui::Button("プリセットをクリア", ImVec2(150, 0))) {
                currentPresetPath_.clear();
                currentPresetName_.clear();
            }
            
            UI::Separator();
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "現在のプリセット: なし");
            UI::Separator();
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

        // ディレクトリパス設定
        ImGui::Text("保存先ディレクトリ");
        if (UI::InputText("##Directory", directoryPathBuffer_, sizeof(directoryPathBuffer_))) {
            needUpdateFileList_ = true;
        }

        UI::Separator();

        // 保存セクション
        ImGui::Text("=== 保存 ===");
        UI::InputText("ファイル名", saveFileNameBuffer_, sizeof(saveFileNameBuffer_));
        
        if (ImGui::Button("プリセットを保存", ImVec2(200, 0))) {
            std::string fileName = std::string(saveFileNameBuffer_);
            if (!fileName.empty()) {
                // 拡張子がない場合は追加
                if (fileName.find(".json") == std::string::npos) {
                    fileName += ".json";
                }
                
                std::string fullPath = std::string(directoryPathBuffer_) + fileName;
                
                // ディレクトリが存在しない場合は作成
                try {
                    std::filesystem::create_directories(directoryPathBuffer_);
                } catch (const std::exception& e) {
                    std::cerr << "Failed to create directory: " << e.what() << std::endl;
                }
                
                if (SavePreset(postEffectManager, fullPath)) {
                    ImGui::OpenPopup("保存成功");
                } else {
                    ImGui::OpenPopup("保存失敗");
                }
            }
        }

        if (auto m = UI::Scope::ModalScope("保存成功", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットを保存しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        }

        if (auto m = UI::Scope::ModalScope("保存失敗", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("プリセットの保存に失敗しました。");
            if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        }

        UI::Separator();

        // 読み込みセクション
        ImGui::Text("=== 読み込み ===");

        // ファイルリスト更新ボタン
        if (ImGui::Button("リストを更新") || needUpdateFileList_) {
            UpdatePresetFileList();
            needUpdateFileList_ = false;
        }

        // プリセットファイルリスト表示
        if (!presetFileList_.empty()) {
            if (auto child = UI::Scope::ChildScope("PresetList", ImVec2(0, 150), ImGuiChildFlags_Border)) {
                for (size_t i = 0; i < presetFileList_.size(); ++i) {
                    bool isSelected = (selectedPresetIndex_ == static_cast<int>(i));
                    if (ImGui::Selectable(GetFileNameWithoutExtension(presetFileList_[i]).c_str(), isSelected)) {
                        selectedPresetIndex_ = static_cast<int>(i);
                    }
                }
            }

            // 選択中のファイルを表示
            if (selectedPresetIndex_ >= 0 && selectedPresetIndex_ < static_cast<int>(presetFileList_.size())) {
                ImGui::Text("選択: %s", presetFileList_[selectedPresetIndex_].c_str());
                
                if (ImGui::Button("プリセットを読み込み", ImVec2(200, 0))) {
                    std::string fullPath = std::string(directoryPathBuffer_) + presetFileList_[selectedPresetIndex_];
                    if (LoadPreset(postEffectManager, fullPath)) {
                        ImGui::OpenPopup("読み込み成功");
                    } else {
                        ImGui::OpenPopup("読み込み失敗");
                    }
                }
            }
        } else {
            ImGui::Text("プリセットファイルが見つかりません。");
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
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
#else
    (void)postEffectManager; // 未使用警告を抑制
#endif // USE_IMGUI
}

void PostEffectPresetManager::UpdatePresetFileList()
{
    presetFileList_ = GetPresetList(directoryPathBuffer_);
    selectedPresetIndex_ = -1;
}

std::string PostEffectPresetManager::GetFileNameWithoutExtension(const std::string& filename)
{
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        return filename.substr(0, lastDot);
    }
    return filename;
}

bool PostEffectPresetManager::SaveCurrentPreset(PostEffectManager* postEffectManager)
{
    if (currentPresetPath_.empty()) {
        std::cerr << "No preset is currently loaded" << std::endl;
        return false;
    }

    return SavePreset(postEffectManager, currentPresetPath_);
}
}
