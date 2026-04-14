#ifdef USE_IMGUI

#include "KeyConfigUI.h"
#include "Input/InputQuery.h"
#include "Utility/Debug/ImGui/ImGuiAll.h"
#include <format>

namespace CoreEngine {

    void KeyConfigUI::Draw(InputQuery& query) {
        auto& config = query.GetConfig();

        // リスニング中の入力検出
        if (isListening_) {
            auto detected = query.DetectAnyInput();
            if (detected.has_value()) {
                auto bindings = config.GetBindings(listeningAction_);
                std::vector<InputBinding> mutable_bindings(bindings.begin(), bindings.end());
                if (listeningIndex_ >= 0 && listeningIndex_ < static_cast<int>(mutable_bindings.size())) {
                    mutable_bindings[listeningIndex_] = detected.value();
                } else {
                    mutable_bindings.push_back(detected.value());
                }
                config.SetBindings(listeningAction_, std::move(mutable_bindings));
                isListening_ = false;
                listeningAction_ = InputAction::Count;
                listeningIndex_ = -1;
            }
            // Escapeキーでキャンセル
            if (query.IsKeyTriggered(DIK_ESCAPE)) {
                isListening_ = false;
                listeningAction_ = InputAction::Count;
                listeningIndex_ = -1;
            }
        }

        // ヘッダー
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "キーコンフィグ");
        UI::Separator();

        if (isListening_) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                ">> 割り当てるキー / ボタンを押してください（Escapeでキャンセル）");
            UI::Spacing();
        }

        // アクション一覧テーブル
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_BordersOuter
            | ImGuiTableFlags_BordersInnerH
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Resizable;

        if (auto table = UI::Scope::TableScope("##KeyConfigTable", 3, tableFlags)) {
            ImGui::TableSetupColumn("アクション", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("バインディング", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Controls", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < static_cast<uint32_t>(InputAction::Count); ++i) {
                const InputAction action = static_cast<InputAction>(i);
                const std::string_view displayName = InputActionToDisplayName(action);

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                // アクション名（日本語表示）
                ImGui::TableSetColumnIndex(0);
                UI::Label(std::string(displayName).c_str());

                // バインディング一覧
                ImGui::TableSetColumnIndex(1);
                const auto& bindings = config.GetBindings(action);
                for (int bi = 0; bi < static_cast<int>(bindings.size()); ++bi) {
                    if (bi > 0) UI::SameLine(0.0f, 4.0f);

                    const std::string label = bindings[bi].Serialize();
                    const bool isThisListening = (isListening_ && listeningAction_ == action && listeningIndex_ == bi);

                    if (isThisListening) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                        ImGui::SmallButton("...");
                        ImGui::PopStyleColor();
                    } else {
                        const std::string btnId = std::format("{}##{}", label, bi);
                        if (ImGui::SmallButton(btnId.c_str())) {
                            isListening_ = true;
                            listeningAction_ = action;
                            listeningIndex_ = bi;
                        }
                    }
                }

                // コントロール列（追加・削除）
                ImGui::TableSetColumnIndex(2);
                if (!isListening_) {
                    if (ImGui::SmallButton("追加")) {
                        isListening_ = true;
                        listeningAction_ = action;
                        listeningIndex_ = -1;
                    }
                    if (!bindings.empty()) {
                        UI::SameLine(0.0f, 4.0f);
                        if (ImGui::SmallButton("削除")) {
                            std::vector<InputBinding> mutable_bindings(bindings.begin(), bindings.end());
                            mutable_bindings.pop_back();
                            config.SetBindings(action, std::move(mutable_bindings));
                        }
                    }
                }

                ImGui::PopID();
            }
        }

        UI::Spacing();
        UI::Separator();
        UI::Spacing();

        // ファイル操作
        if (ImGui::Button("保存")) {
            config.SaveToFile(configFilePath_);
        }
        UI::SameLine();
        if (ImGui::Button("読み込み")) {
            config.LoadFromFile(configFilePath_);
        }
        UI::SameLine();
        if (ImGui::Button("初期値に戻す")) {
            config.ResetToDefault();
        }
        UI::SameLine();
        ImGui::TextDisabled("(%s)", configFilePath_.c_str());
    }

}

#endif // USE_IMGUI
