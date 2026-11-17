#include "pch.h"
#include "AnimatorInspectorPanel.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"
#include "AnimatorWidgets.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    void AnimatorInspectorPanel::Draw(AnimatorDocument& document)
    {
        const AnimatorLayer& layer = document.data.layers.front();
        switch (document.selection) {
        case AnimatorSelection::State:
            if (document.selected >= 0 && document.selected < static_cast<int>(layer.states.size())) {
                DrawStateInspector(document, document.selected);
                return;
            }
            break;
        case AnimatorSelection::AnyState:
            DrawAnyStateInspector(document);
            return;
        case AnimatorSelection::Transition:
            if (document.selected >= 0 && document.selected < static_cast<int>(layer.transitions.size())) {
                DrawTransitionInspector(document, document.selected);
                return;
            }
            break;
        case AnimatorSelection::None:
            break;
        }
        DrawControllerInspector(document);
    }

    void AnimatorInspectorPanel::DrawControllerInspector(AnimatorDocument& document)
    {
        AnimatorLayer& layer = document.data.layers.front();
        UI::SectionHeader("設定");
        ImGui::TextWrapped("%s", document.assetPath.c_str());
        ImGui::Spacing();

        const std::string preview = layer.defaultState.empty() ? std::string("（なし）") : layer.defaultState;
        if (ImGui::BeginCombo("最初の状態", preview.c_str())) {
            for (size_t i = 0; i < layer.states.size(); ++i) {
                if (ImGui::Selectable(layer.states[i].name.c_str(), layer.states[i].name == layer.defaultState)) {
                    document.SetDefaultState(static_cast<int>(i));
                }
            }
            ImGui::EndCombo();
        }
        if (AnimatorWidgets::ModelField("見本のモデル", document.data.previewModel)) {
            document.MarkChanged("見本のモデルを変える");
        }
        UI::Hint("空のクリップを足すときのファイルに使います。");

        ImGui::Spacing();
        ImGui::TextWrapped("状態 %zu 個・遷移 %zu 本・パラメータ %zu 個・クリップ %zu 本", layer.states.size(), layer.transitions.size(),
            document.data.parameters.size(), document.data.clips.size());

        UI::SectionHeader("使い方");
        ImGui::PushTextWrapPos(0.0f);
        UI::Hint("・キャンバスの右クリックで状態を作ります。");
        UI::Hint("・状態の右クリック > 遷移を引く のあと、移る先の状態をクリックします。");
        UI::Hint("・遷移を選ぶと、条件・混ぜる秒数・終わりの時刻を決められます。");
        UI::Hint("・ホイールで拡大縮小、中ボタンか空いた所の左ドラッグで動かします。");
        UI::Hint("・スクリプトからは animator.SetFloat(\"Speed\", 1.0f) のように値を書きます。");
        ImGui::PopTextWrapPos();
    }

    void AnimatorInspectorPanel::DrawStateInspector(AnimatorDocument& document, int stateIndex)
    {
        AnimatorLayer& layer = document.data.layers.front();
        UI::SectionHeader("状態");
        {
            std::string newName;
            if (AnimatorWidgets::NameField(document, "名前", layer.states[static_cast<size_t>(stateIndex)].name, newName)) {
                document.RenameState(stateIndex, newName);
            }
        }
        AnimatorState& state = layer.states[static_cast<size_t>(stateIndex)];

        const std::string clipPreview = state.clip.empty() ? std::string("（なし: 骨の初期の姿勢）") : state.clip;
        if (ImGui::BeginCombo("クリップ", clipPreview.c_str())) {
            if (ImGui::Selectable("（なし: 骨の初期の姿勢）", state.clip.empty())) {
                state.clip.clear();
                document.MarkChanged("状態のクリップを選ぶ");
            }
            for (const AnimatorClip& clip : document.data.clips) {
                if (ImGui::Selectable(clip.id.c_str(), clip.id == state.clip)) {
                    state.clip = clip.id;
                    document.MarkChanged("状態のクリップを選ぶ");
                }
            }
            ImGui::EndCombo();
        }
        const AnimatorClip* const clip = document.data.FindClip(state.clip);
        if (!state.clip.empty() && !clip) {
            ImGui::TextColored(Theme::kWarn, "クリップ「%s」がありません", state.clip.c_str());
        }
        if (clip) {
            const int clipIndex = static_cast<int>(clip - document.data.clips.data());
            ImGui::Indent();
            ImGui::PushID("clip");
            AnimatorWidgets::ClipFields(document, clipIndex, false);
            ImGui::PopID();
            ImGui::Unindent();
        }
        AnimatorState& current = layer.states[static_cast<size_t>(stateIndex)];
        if (ImGui::DragFloat("速さ", &current.speed, 0.01f, -4.0f, 4.0f, "%.2f")) {
            document.MarkChanged("状態の速さを変える");
        }
        if (ImGui::Checkbox("ループ", &current.loop)) {
            document.MarkChanged("状態のループを変える");
        }
        if (current.name == layer.defaultState) {
            ImGui::TextColored(Theme::kWarm, "最初の状態です");
        } else if (ImGui::Button("最初の状態にする")) {
            document.SetDefaultState(stateIndex);
        }

        UI::SectionHeader("出ていく遷移");
        const std::string name = current.name;
        DrawTransitionList(document, name);
        if (ImGui::Button("遷移を引く")) {
            document.linkFrom = stateIndex;
        }
        ImGui::PushTextWrapPos(0.0f);
        UI::Hint("押してから、移る先の状態をキャンバスでクリックします。上の遷移ほど先に調べます。");
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        UI::Separator();
        if (ImGui::Button("この状態を消す")) {
            document.DeleteState(stateIndex);
        }
    }

    void AnimatorInspectorPanel::DrawAnyStateInspector(AnimatorDocument& document)
    {
        UI::SectionHeader("どこからでも");
        ImGui::PushTextWrapPos(0.0f);
        UI::Hint("ここから出る遷移は、どの状態にいても調べます（ほかの遷移より先）。");
        UI::Hint("移る先が今の状態なら移りません。");
        ImGui::PopTextWrapPos();
        UI::SectionHeader("出ていく遷移");
        DrawTransitionList(document, AnimatorControllerFormat::kAnyState);
        if (ImGui::Button("遷移を引く")) {
            document.linkFrom = -1;
        }
    }

    void AnimatorInspectorPanel::DrawTransitionList(AnimatorDocument& document, const std::string& from)
    {
        const AnimatorLayer& layer = document.data.layers.front();
        std::vector<int> indices;
        for (size_t t = 0; t < layer.transitions.size(); ++t) {
            if (layer.transitions[t].from == from) {
                indices.push_back(static_cast<int>(t));
            }
        }
        if (indices.empty()) {
            UI::Hint("まだありません");
            return;
        }
        const float buttonWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        for (size_t k = 0; k < indices.size(); ++k) {
            const int t = indices[k];
            const AnimatorTransition& transition = layer.transitions[static_cast<size_t>(t)];
            ImGui::PushID(t);
            const bool selected = document.selection == AnimatorSelection::Transition && document.selected == t;
            const std::string label = "→ " + transition.to + "（" + AnimatorWidgets::TransitionSummary(transition, document.data) + "）";
            const float width = std::max(40.0f, ImGui::GetContentRegionAvail().x - (buttonWidth + spacing) * 2.0f);
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(width, 0.0f))) {
                document.Select(AnimatorSelection::Transition, t);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(k == 0);
            const bool up = ImGui::ArrowButton("##up", ImGuiDir_Up);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(k + 1 == indices.size());
            const bool down = ImGui::ArrowButton("##down", ImGuiDir_Down);
            ImGui::EndDisabled();
            ImGui::PopID();
            if (up || down) {
                document.MoveTransition(t, up ? -1 : 1);
                break;
            }
        }
    }

    void AnimatorInspectorPanel::DrawTransitionInspector(AnimatorDocument& document, int transitionIndex)
    {
        AnimatorLayer& layer = document.data.layers.front();
        AnimatorTransition& transition = layer.transitions[static_cast<size_t>(transitionIndex)];
        const bool fromAny = transition.from == AnimatorControllerFormat::kAnyState;
        UI::SectionHeader("遷移");
        ImGui::Text("%s → %s", fromAny ? "どこからでも" : transition.from.c_str(), transition.to.c_str());
        if (!fromAny && AnimatorStateIndexOf(layer, transition.from) < 0) {
            ImGui::TextColored(Theme::kWarn, "元の状態「%s」がありません", transition.from.c_str());
        }
        if (AnimatorStateIndexOf(layer, transition.to) < 0) {
            ImGui::TextColored(Theme::kWarn, "先の状態「%s」がありません", transition.to.c_str());
        }

        if (ImGui::DragFloat("混ぜる秒数", &transition.duration, 0.005f, 0.0f, 10.0f, "%.3f 秒")) {
            transition.duration = std::max(0.0f, transition.duration);
            document.MarkChanged("遷移の混ぜる秒数を変える");
        }
        if (ImGui::Checkbox("終わりの時刻で移る", &transition.hasExitTime)) {
            document.MarkChanged("遷移の終わりの時刻を変える");
        }
        if (transition.hasExitTime) {
            if (ImGui::DragFloat("終わりの時刻", &transition.exitTime, 0.01f, 0.0f, 10.0f, "%.2f")) {
                transition.exitTime = std::max(0.0f, transition.exitTime);
                document.MarkChanged("遷移の終わりの時刻を変える");
            }
            ImGui::PushTextWrapPos(0.0f);
            UI::Hint("元の状態の進みの割合です（1 で 1 回分の終わり）。条件があれば、両方そろったときに移ります。");
            ImGui::PopTextWrapPos();
        }
        if (ImGui::SliderFloat("先の始まり", &transition.offset, 0.0f, 1.0f, "%.2f")) {
            transition.offset = std::clamp(transition.offset, 0.0f, 1.0f);
            document.MarkChanged("遷移の先の始まりを変える");
        }
        UI::Tooltip("移った先のクリップを、どの割合から始めるか");

        UI::SectionHeader("条件（全部そろったら移る）");
        if (transition.conditions.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            if (transition.hasExitTime) {
                UI::Hint("条件なし: 終わりの時刻で移ります。");
            } else {
                ImGui::TextColored(Theme::kWarn, "条件も終わりの時刻もありません。すぐ移ります。");
            }
            ImGui::PopTextWrapPos();
        }
        const std::vector<AnimatorParameter>& parameters = document.data.parameters;
        const float deleteWidth = ImGui::GetFrameHeight();
        for (size_t c = 0; c < transition.conditions.size(); ++c) {
            ImGui::PushID(static_cast<int>(c));
            AnimatorCondition& condition = transition.conditions[c];
            const AnimatorParameter* parameter = document.data.FindParameter(condition.parameter);
            const float rowWidth = ImGui::GetContentRegionAvail().x - deleteWidth - ImGui::GetStyle().ItemSpacing.x;

            ImGui::SetNextItemWidth(rowWidth * 0.42f);
            if (ImGui::BeginCombo("##parameter", condition.parameter.empty() ? "（選ぶ）" : condition.parameter.c_str())) {
                for (const AnimatorParameter& candidate : parameters) {
                    if (ImGui::Selectable(candidate.name.c_str(), candidate.name == condition.parameter)) {
                        condition.parameter = candidate.name;
                        if (!AnimatorControllerFormat::IsValidOp(candidate.type, condition.op)) {
                            condition.op = AnimatorWidgets::FirstOp(candidate.type);
                        }
                        document.MarkChanged("条件のパラメータを選ぶ");
                    }
                }
                ImGui::EndCombo();
            }
            parameter = document.data.FindParameter(condition.parameter);
            if (parameter) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(rowWidth * 0.24f);
                if (ImGui::BeginCombo("##op", AnimatorWidgets::OpLabel(condition.op))) {
                    for (AnimatorConditionOp op : AnimatorWidgets::kAllOps) {
                        if (!AnimatorControllerFormat::IsValidOp(parameter->type, op)) {
                            continue;
                        }
                        if (ImGui::Selectable(AnimatorWidgets::OpLabel(op), op == condition.op)) {
                            condition.op = op;
                            document.MarkChanged("条件の比べ方を選ぶ");
                        }
                    }
                    ImGui::EndCombo();
                }
                if (AnimatorWidgets::OpHasValue(condition.op)) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(std::max(30.0f, ImGui::GetContentRegionAvail().x - deleteWidth - ImGui::GetStyle().ItemSpacing.x));
                    if (parameter->type == AnimatorParameterType::Int) {
                        int value = static_cast<int>(std::lround(condition.value));
                        if (ImGui::DragInt("##value", &value, 0.1f)) {
                            condition.value = static_cast<float>(value);
                            document.MarkChanged("条件の値を変える");
                        }
                    } else if (ImGui::DragFloat("##value", &condition.value, 0.01f)) {
                        document.MarkChanged("条件の値を変える");
                    }
                }
            }
            ImGui::SameLine();
            const bool remove = ImGui::Button("×", ImVec2(deleteWidth, 0.0f));
            if (!parameter) {
                ImGui::TextColored(Theme::kWarn, "パラメータ「%s」がありません", condition.parameter.c_str());
            } else if (!AnimatorControllerFormat::IsValidOp(parameter->type, condition.op)) {
                ImGui::TextColored(Theme::kWarn, "%s には「%s」で比べられません", AnimatorWidgets::TypeLabel(parameter->type), AnimatorWidgets::OpLabel(condition.op));
            }
            ImGui::PopID();
            if (remove) {
                transition.conditions.erase(transition.conditions.begin() + static_cast<std::ptrdiff_t>(c));
                document.MarkChanged("条件を消す");
                break;
            }
        }
        if (ImGui::Button("条件を足す")) {
            AnimatorCondition condition;
            if (!parameters.empty()) {
                condition.parameter = parameters.front().name;
                condition.op = AnimatorWidgets::FirstOp(parameters.front().type);
            }
            transition.conditions.push_back(std::move(condition));
            document.MarkChanged("条件を足す");
        }
        if (parameters.empty()) {
            UI::Hint("先に左でパラメータを足してください。");
        }

        ImGui::Spacing();
        UI::Separator();
        if (ImGui::Button("この遷移を消す")) {
            document.DeleteTransition(transitionIndex);
        }
    }
}

#endif // CORE_EDITOR
