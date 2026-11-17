#include "pch.h"
#include "AnimatorParameterPanel.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"
#include "AnimatorLiveView.h"
#include "AnimatorWidgets.h"

#include "Animation/AnimatorRuntime.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr const char* kAddParameterMenu = "##AnimatorAddParameter";
    }

    void AnimatorParameterPanel::Draw(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        DrawParameters(document, live);
        DrawClips(document);
    }

    void AnimatorParameterPanel::DrawParameters(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        UI::SectionHeader("パラメータ");
        if (ImGui::Button("パラメータを足す")) {
            ImGui::OpenPopup(kAddParameterMenu);
        }
        if (ImGui::BeginPopup(kAddParameterMenu)) {
            for (AnimatorParameterType type : { AnimatorParameterType::Float, AnimatorParameterType::Int,
                     AnimatorParameterType::Bool, AnimatorParameterType::Trigger }) {
                if (ImGui::MenuItem(AnimatorWidgets::TypeLabel(type))) {
                    document.AddParameter(type);
                }
            }
            ImGui::EndPopup();
        }

        std::vector<AnimatorParameter>& parameters = document.data.parameters;
        if (parameters.empty()) {
            UI::Hint("スクリプトから書く値です。");
            UI::Hint("遷移の条件に使います。");
        }

        const AnimatorControllerData* const running = live.runtime ? live.runtime->GetController() : nullptr;
        const float deleteWidth = ImGui::GetFrameHeight();
        for (size_t i = 0; i < parameters.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            AnimatorParameter& parameter = parameters[i];
            ImGui::TextColored(AnimatorWidgets::TypeColor(parameter.type), "%s", AnimatorWidgets::TypeGlyph(parameter.type));
            UI::Tooltip(AnimatorWidgets::TypeLabel(parameter.type));
            ImGui::SameLine();

            const float rowWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(rowWidth * 0.5f);
            std::string newName;
            if (AnimatorWidgets::NameField(document, "##name", parameter.name, newName)) {
                document.RenameParameter(static_cast<int>(i), newName);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(30.0f, ImGui::GetContentRegionAvail().x - deleteWidth - ImGui::GetStyle().ItemSpacing.x));

            // 再生中は、動いている設定に同じ名前と種類のパラメータがあれば今の値を出す
            const AnimatorParameter* const runningParameter = running ? running->FindParameter(parameter.name) : nullptr;
            if (live.runtime && runningParameter && runningParameter->type == parameter.type) {
                AnimatorRuntime& runtime = *live.runtime;
                switch (parameter.type) {
                case AnimatorParameterType::Float: {
                    float value = runtime.GetFloat(parameter.name);
                    if (ImGui::DragFloat("##value", &value, 0.01f)) {
                        runtime.SetFloat(parameter.name, value);
                    }
                    break;
                }
                case AnimatorParameterType::Int: {
                    int value = runtime.GetInt(parameter.name);
                    if (ImGui::DragInt("##value", &value, 0.1f)) {
                        runtime.SetInt(parameter.name, value);
                    }
                    break;
                }
                case AnimatorParameterType::Bool: {
                    bool value = runtime.GetBool(parameter.name);
                    if (ImGui::Checkbox("##value", &value)) {
                        runtime.SetBool(parameter.name, value);
                    }
                    break;
                }
                case AnimatorParameterType::Trigger: {
                    const bool raised = runtime.GetBool(parameter.name);
                    ImGui::BeginDisabled(raised);
                    if (ImGui::Button(raised ? "立っている" : "立てる", ImVec2(ImGui::CalcItemWidth(), 0.0f))) {
                        runtime.SetTrigger(parameter.name);
                    }
                    ImGui::EndDisabled();
                    break;
                }
                }
            } else if (live.runtime) {
                ImGui::TextDisabled("保存すると使えます");
            } else {
                switch (parameter.type) {
                case AnimatorParameterType::Float:
                    if (ImGui::DragFloat("##value", &parameter.defaultFloat, 0.01f)) {
                        document.MarkChanged("パラメータの初めの値を変える");
                    }
                    break;
                case AnimatorParameterType::Int:
                    if (ImGui::DragInt("##value", &parameter.defaultInt, 0.1f)) {
                        document.MarkChanged("パラメータの初めの値を変える");
                    }
                    break;
                case AnimatorParameterType::Bool:
                    if (ImGui::Checkbox("##value", &parameter.defaultBool)) {
                        document.MarkChanged("パラメータの初めの値を変える");
                    }
                    break;
                case AnimatorParameterType::Trigger:
                    ImGui::TextDisabled("トリガー");
                    break;
                }
            }
            UI::Tooltip("初めの値（再生中は今の値）");

            ImGui::SameLine();
            if (ImGui::Button("×", ImVec2(deleteWidth, 0.0f))) {
                document.DeleteParameter(static_cast<int>(i));
                ImGui::PopID();
                break;
            }
            UI::Tooltip("このパラメータを消す（遷移の条件は残り、警告が出ます）");
            ImGui::PopID();
        }
        if (live.runtime) {
            UI::Hint("再生中は今の値です。変えると今の動きに効きます。");
        }
    }

    void AnimatorParameterPanel::DrawClips(AnimatorDocument& document)
    {
        ImGui::Spacing();
        UI::SectionHeader("クリップ");
        ImGui::Button("モデルをここへドラッグ", ImVec2(-1.0f, 0.0f));
        UI::Tooltip("Project からモデルを落とすと、中のアニメーションを全部クリップに足します。キャンバスへ落とすと状態も作ります。");
        if (ImGui::BeginDragDropTarget()) {
            if (const std::optional<std::string> model = AnimatorWidgets::AcceptModelDrop()) {
                document.AddClipsFromModel(*model, nullptr);
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::Button("空のクリップを足す", ImVec2(-1.0f, 0.0f))) {
            document.AddEmptyClip();
        }

        std::vector<AnimatorClip>& clips = document.data.clips;
        if (clips.empty()) {
            UI::Hint("状態が流すアニメーションの表です。");
        }
        for (size_t i = 0; i < clips.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool open = ImGui::TreeNodeEx("##clip", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", clips[i].id.c_str());
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload(AnimatorWidgets::kClipPayload, clips[i].id.c_str(), clips[i].id.size() + 1);
                ImGui::Text("状態を作る: %s", clips[i].id.c_str());
                ImGui::EndDragDropSource();
            }
            UI::Tooltip("キャンバスへドラッグすると、このクリップを流す状態を作ります。");
            if (open) {
                const bool removed = AnimatorWidgets::ClipFields(document, static_cast<int>(i), true);
                ImGui::TreePop();
                if (removed) {
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::PopID();
        }
    }
}

#endif // CORE_EDITOR
