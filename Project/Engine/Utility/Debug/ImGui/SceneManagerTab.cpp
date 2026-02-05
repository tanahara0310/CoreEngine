#include "SceneManagerTab.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>
#include <algorithm>


namespace CoreEngine
{
void SceneManagerTab::Initialize(SceneManager* sceneManager)
{
    sceneManager_ = sceneManager;
    isChangeRequested_ = false;
    requestedSceneName_.clear();
    selectedSceneIndex_ = 0;
    previousSceneName_.clear();
}

void SceneManagerTab::DrawImGui()
{
    if (!sceneManager_) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "SceneManager が初期化されていません");
        return;
    }

    // 現在のシーン名が変更された場合のみ選択インデックスを更新
    UpdateCurrentScene();

    // ウィンドウ全体のパディング調整
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

    // ヘッダー部分
    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Scene Manager");
    ImGui::Separator();
    ImGui::Spacing();

    // 現在のシーン情報
    DrawCurrentSceneInfo();
    ImGui::Spacing();

    // シーン切り替えコントロール
    DrawSceneChangeControl();
    ImGui::Spacing();

    // シーン操作ボタン
    DrawSceneOperations();
    ImGui::Spacing();

    // シーン一覧
    if (ImGui::CollapsingHeader("登録シーン一覧", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawSceneList();
    }

    ImGui::PopStyleVar(2);
}

void SceneManagerTab::UpdateCurrentScene()
{
    std::string currentSceneName = sceneManager_->GetCurrentSceneName();
    
    // シーン名が変更された場合のみ選択インデックスを更新
    if (currentSceneName != previousSceneName_) {
        previousSceneName_ = currentSceneName;
        
        std::vector<std::string> sceneNames = sceneManager_->GetAllSceneNames();
        for (int i = 0; i < static_cast<int>(sceneNames.size()); ++i) {
            if (sceneNames[i] == currentSceneName) {
                selectedSceneIndex_ = i;
                break;
            }
        }
    }
}

void SceneManagerTab::DrawCurrentSceneInfo()
{
    std::string currentSceneName = sceneManager_->GetCurrentSceneName();
    
    ImGui::Text("現在のシーン:");
    ImGui::SameLine();
    
    if (currentSceneName == "None") {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", currentSceneName.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.2f, 1.0f), "%s", currentSceneName.c_str());
    }
}

void SceneManagerTab::DrawSceneChangeControl()
{
    std::vector<std::string> sceneNames = sceneManager_->GetAllSceneNames();
    
    if (sceneNames.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "登録されているシーンがありません");
        return;
    }

    std::string currentSceneName = sceneManager_->GetCurrentSceneName();
    
    const char* previewValue = selectedSceneIndex_ >= 0 && selectedSceneIndex_ < static_cast<int>(sceneNames.size())
        ? sceneNames[selectedSceneIndex_].c_str()
        : "シーンを選択...";

    ImGui::Text("シーン切り替え:");
    ImGui::SetNextItemWidth(-100.0f);
    
    if (ImGui::BeginCombo("##SceneCombo", previewValue)) {
        for (int i = 0; i < static_cast<int>(sceneNames.size()); ++i) {
            bool isSelected = (selectedSceneIndex_ == i);
            bool isCurrent = (sceneNames[i] == currentSceneName);
            
            std::string label = isCurrent ? "[Current] " + sceneNames[i] : sceneNames[i];
            
            if (ImGui::Selectable(label.c_str(), isSelected)) {
                selectedSceneIndex_ = i;
            }
            
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    
    bool canChange = selectedSceneIndex_ >= 0 && selectedSceneIndex_ < static_cast<int>(sceneNames.size());
    if (!canChange) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("切り替え", ImVec2(90.0f, 0))) {
        if (canChange) {
            requestedSceneName_ = sceneNames[selectedSceneIndex_];
            isChangeRequested_ = true;
        }
    }
    
    if (!canChange) {
        ImGui::EndDisabled();
    }
}

void SceneManagerTab::DrawSceneOperations()
{
    std::string currentSceneName = sceneManager_->GetCurrentSceneName();
    bool hasCurrentScene = (currentSceneName != "None");

    // リセットボタン
    if (ImGui::Button("リセット", ImVec2(90.0f, 0))) {
        isChangeRequested_ = true;
        requestedSceneName_ = "None";
    }

    ImGui::SameLine();

    // リロードボタン
    if (!hasCurrentScene) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("再読み込み", ImVec2(120.0f, 0))) {
        if (hasCurrentScene) {
            requestedSceneName_ = currentSceneName;
            isChangeRequested_ = true;
        }
    }

    if (!hasCurrentScene) {
        ImGui::EndDisabled();
    }
}

void SceneManagerTab::DrawSceneList()
{
    ImGui::Indent(10.0f);

    std::vector<std::string> sceneNames = sceneManager_->GetAllSceneNames();
    std::string currentSceneName = sceneManager_->GetCurrentSceneName();

    if (sceneNames.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "シーンが登録されていません");
        ImGui::Unindent(10.0f);
        return;
    }

    // 検索フィルタ
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##SceneSearch", "シーンを検索...", sceneSearchBuffer_, sizeof(sceneSearchBuffer_));
    
    std::string searchQuery = sceneSearchBuffer_;
    std::transform(searchQuery.begin(), searchQuery.end(), searchQuery.begin(), 
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    ImGui::Spacing();

    // スクロール可能なシーンリスト
    if (ImGui::BeginChild("SceneListChild", ImVec2(0, 200), true)) {
        for (size_t i = 0; i < sceneNames.size(); ++i) {
            const std::string& sceneName = sceneNames[i];
            
            // 検索フィルタリング
            if (!searchQuery.empty()) {
                std::string lowerSceneName = sceneName;
                std::transform(lowerSceneName.begin(), lowerSceneName.end(), lowerSceneName.begin(), 
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lowerSceneName.find(searchQuery) == std::string::npos) {
                    continue;
                }
            }

            bool isCurrent = (sceneName == currentSceneName);

            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.8f, 0.2f, 1.0f));
                ImGui::Text("[Current] %s", sceneName.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::Selectable(sceneName.c_str(), false);
                
                // ダブルクリックでシーン切り替え
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    requestedSceneName_ = sceneName;
                    isChangeRequested_ = true;
                }

                // 右クリックメニュー
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("このシーンに切り替え")) {
                        requestedSceneName_ = sceneName;
                        isChangeRequested_ = true;
                    }
                    ImGui::EndPopup();
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::Unindent(10.0f);
}
}
