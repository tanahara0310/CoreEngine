#include "pch.h"
#ifdef CORE_EDITOR

#include "KeyConfigUI.h"
#include "Input/InputQuery.h"
#include "Editor/ImGui/ImGuiAll.h"

#include <string>
#include <unordered_map>

namespace CoreEngine {

    namespace {

        /// 画面に並べる場面の順
        constexpr InputContext kSceneOrder[] = {
            InputContext::Game,
            InputContext::UI,
            InputContext::Editor,
        };

        /// @brief そのアクションを並べる場面（複数に属するものは先の場面へ置く）
        InputContext PrimaryContext(InputContext contexts) {
            for (const InputContext scene : kSceneOrder) {
                if (Overlaps(contexts, scene)) {
                    return scene;
                }
            }
            return InputContext::Game;
        }

        /// @brief 割り当ての綴り → それを使っているアクション
        std::unordered_map<std::string, std::vector<InputAction>>
        CollectBindingOwners(const InputConfig& config) {
            std::unordered_map<std::string, std::vector<InputAction>> owners;
            for (std::size_t i = 0; i < InputActions::Count(); ++i) {
                const InputAction action = static_cast<InputAction>(i);
                for (const InputBinding& binding : config.GetBindings(action)) {
                    owners[binding.Serialize()].push_back(action);
                }
            }
            return owners;
        }

        /// @brief その割り当てとぶつかる相手の名前
        /// @details 場面が重ならなければ同じボタンでもぶつからないので、外す。
        /// @return ぶつかる相手を " / " で繋いだもの（無ければ空）
        std::string DescribeConflict(
            const std::unordered_map<std::string, std::vector<InputAction>>& owners,
            const std::string& text, InputAction self) {
            const auto found = owners.find(text);
            if (found == owners.end()) {
                return {};
            }
            const InputContext mine = InputActionToContexts(self);
            std::string names;
            for (const InputAction other : found->second) {
                if (other == self || !Overlaps(InputActionToContexts(other), mine)) {
                    continue;
                }
                if (!names.empty()) {
                    names += " / ";
                }
                names += InputActionToDisplayName(other);
            }
            return names;
        }
    }

    void KeyConfigUI::Draw(InputQuery& query) {
        auto& config = query.GetConfig();

        // リスニング中の入力検出
        if (isListening_) {
            // Escape は画面表記どおりキャンセルを優先する。
            // DetectAnyInput() は Escape も拾うため、先に判定しないと
            // キャンセルのつもりが Escape の割り当てになってしまう
            // この画面は ImGui の上にあるので、普通に読むと必ず外される
            if (query.IsKeyTriggeredRaw(DIK_ESCAPE)) {
                StopListening();
            } else if (auto detected = query.DetectAnyInput()) {
                std::vector<InputBinding> newBindings = config.GetBindings(listeningAction_);
                if (listeningIndex_ >= 0 && listeningIndex_ < static_cast<int>(newBindings.size())) {
                    newBindings[listeningIndex_] = *detected;
                } else {
                    newBindings.push_back(*detected);
                }
                ApplyBindings(config, listeningAction_, std::move(newBindings));
                StopListening();
            }
        }

        // ヘッダー
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "キーコンフィグ");
        UI::Separator();

        ImGui::TextDisabled("バインディングをクリックで再割り当て / x で削除。変更は自動保存されます。");
        UI::Spacing();

        if (isListening_) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                ">> 割り当てるキー / ボタン / スティックを入力してください（Escapeでキャンセル）");
            UI::Spacing();
        }

        if (saveFailed_) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "!! 保存に失敗しました: %s", configFilePath_.c_str());
            UI::Spacing();
        }

        // アクション一覧テーブル
        constexpr ImGuiTableFlags tableFlags =
            ImGuiTableFlags_BordersOuter
            | ImGuiTableFlags_BordersInnerH
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Resizable;

        // 同じ入力が、場面の重なる別のアクションにも割り当たっていないかを先に集める
        const auto owners = CollectBindingOwners(config);

        if (auto table = UI::Scope::TableScope("##KeyConfigTable", 3, tableFlags)) {
            ImGui::TableSetupColumn("アクション", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("バインディング", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Controls", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            // 場面ごとにまとめて出す。ゲームの操作を探すときにエディタの行が混ざらない
            for (const InputContext scene : kSceneOrder) {
            bool sceneHeaderDrawn = false;
            for (uint32_t i = 0; i < static_cast<uint32_t>(InputActions::Count()); ++i) {
                const InputAction action = static_cast<InputAction>(i);
                const InputContext contexts = InputActionToContexts(action);
                if (PrimaryContext(contexts) != scene) {
                    continue;
                }
                const std::string_view displayName = InputActionToDisplayName(action);

                if (!sceneHeaderDrawn) {
                    sceneHeaderDrawn = true;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s",
                        std::string(InputContextToDisplayName(scene)).c_str());
                }

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));

                // アクション名（日本語表示）
                ImGui::TableSetColumnIndex(0);
                UI::Label(std::string(displayName).c_str());
                // 2 つの場面で効くものは、どちらでも効くと分かるようにしておく
                if (contexts != scene) {
                    UI::SameLine();
                    ImGui::TextDisabled("(%s)", InputContextToString(contexts).c_str());
                }

                // バインディング一覧（各項目に個別削除の x を付ける）
                ImGui::TableSetColumnIndex(1);
                const auto& bindings = config.GetBindings(action);
                int removeIndex = -1;
                for (int bi = 0; bi < static_cast<int>(bindings.size()); ++bi) {
                    if (bi > 0) UI::SameLine(0.0f, 8.0f);

                    // 同じキーが 2 つ並んでもボタン ID が衝突しないよう添字で分ける
                    ImGui::PushID(bi);

                    const bool isThisListening = (isListening_ && listeningAction_ == action && listeningIndex_ == bi);
                    if (isThisListening) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                        ImGui::SmallButton("...");
                        ImGui::PopStyleColor();
                    } else {
                        const std::string text = bindings[bi].Serialize();
                        const std::string conflict = DescribeConflict(owners, text, action);
                        if (!conflict.empty()) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.24f, 0.2f, 1.0f));
                        }
                        if (ImGui::SmallButton(text.c_str())) {
                            isListening_ = true;
                            listeningAction_ = action;
                            listeningIndex_ = bi;
                        }
                        if (!conflict.empty()) {
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("同じ場面の「%s」と重なっています", conflict.c_str());
                            }
                        }
                        // 入力待ちの間は隠す。削除で添字がずれると
                        // listeningIndex_ が別のバインディングを指してしまうため
                        if (!isListening_) {
                            UI::SameLine(0.0f, 1.0f);
                            if (ImGui::SmallButton("x")) {
                                removeIndex = bi;
                            }
                        }
                    }

                    ImGui::PopID();
                }

                // 走査中に配列を触らないよう、削除はループを抜けてから適用する
                if (removeIndex >= 0) {
                    std::vector<InputBinding> newBindings = bindings;
                    newBindings.erase(newBindings.begin() + removeIndex);
                    ApplyBindings(config, action, std::move(newBindings));
                }

                // コントロール列（追加）
                ImGui::TableSetColumnIndex(2);
                if (!isListening_) {
                    if (ImGui::SmallButton("追加")) {
                        isListening_ = true;
                        listeningAction_ = action;
                        listeningIndex_ = -1;
                    }
                }

                ImGui::PopID();
            }
            }
        }

        UI::Spacing();
        UI::Separator();
        UI::Spacing();

        // ファイル操作（保存は編集のたびに自動で行うのでボタンは持たない）
        if (ImGui::Button("ファイルから再読み込み")) {
            config.LoadFromFile(configFilePath_);
            saveFailed_ = false;
        }
        UI::SameLine();
        if (ImGui::Button("初期値に戻す")) {
            config.ResetToDefault();
            Save(config);
        }
        UI::SameLine();
        ImGui::TextDisabled("(自動保存: %s)", configFilePath_.c_str());
    }

    void KeyConfigUI::StopListening() {
        isListening_ = false;
        listeningAction_ = InputAction::Invalid;
        listeningIndex_ = -1;
    }

    void KeyConfigUI::ApplyBindings(InputConfig& config, InputAction action, std::vector<InputBinding> bindings) {
        config.SetBindings(action, std::move(bindings));
        Save(config);
    }

    void KeyConfigUI::Save(const InputConfig& config) {
        // 自動保存は画面に何も出ないので、失敗だけは気付けるようにフラグで持ち回る
        saveFailed_ = !config.SaveToFile(configFilePath_);
    }

}

#endif // CORE_EDITOR
