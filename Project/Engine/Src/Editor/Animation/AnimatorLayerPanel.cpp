#include "pch.h"
#include "AnimatorLayerPanel.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"
#include "AnimatorLiveView.h"
#include "AnimatorWidgets.h"

#include "Animation/AnimatorRuntime.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Graphics/Model/Skeleton/Skeleton.h"

#include <algorithm>
#include <cstdio>
#include <optional>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 表の右に出す重みとマスクの短い文
        std::string LayerInfo(const AnimatorLayer& layer, int index, const AnimatorLiveView& live)
        {
            if (index == 0) {
                return "土台";
            }
            float weight = layer.weight;
            if (live.runtime) {
                if (const int running = live.runtime->FindLayer(layer.name); running >= 0) {
                    weight = live.runtime->GetLayerWeight(running);
                }
            }
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f", weight);
            return std::string(buffer) + (layer.useMask ? "・マスク" : "");
        }

        void SetSubtree(const Skeleton& skeleton, const std::vector<std::vector<int>>& children, int joint, bool on,
            std::unordered_set<std::string>& enabled)
        {
            const std::string& name = skeleton.joints[static_cast<size_t>(joint)].name;
            if (on) {
                enabled.insert(name);
            } else {
                enabled.erase(name);
            }
            for (const int child : children[static_cast<size_t>(joint)]) {
                SetSubtree(skeleton, children, child, on, enabled);
            }
        }
    }

    void AnimatorLayerPanel::DrawList(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        UI::SectionHeader("レイヤー");
        if (ImGui::Button("レイヤーを足す")) {
            document.AddLayer();
        }
        UI::Tooltip("上に重ねる状態の表を足します。重みとマスクで、体の一部だけ別の動きにできます。");

        enum class Action { None, Up, Down, Delete };
        Action action = Action::None;
        int target = -1;
        const int count = static_cast<int>(document.data.layers.size());
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            const AnimatorLayer& layer = document.data.layers[static_cast<size_t>(i)];
            const std::string info = LayerInfo(layer, i, live);
            const float startX = ImGui::GetCursorPosX();
            const float available = ImGui::GetContentRegionAvail().x;
            const bool active = i == document.activeLayer;
            if (ImGui::Selectable(layer.name.c_str(), active, ImGuiSelectableFlags_AllowOverlap)) {
                document.SetActiveLayer(i);
                document.Select(AnimatorSelection::Layer, i);
            }
            if (ImGui::BeginPopupContextItem("##layerMenu")) {
                if (ImGui::MenuItem("上へ", nullptr, false, i > 1)) {
                    action = Action::Up;
                    target = i;
                }
                if (ImGui::MenuItem("下へ", nullptr, false, i > 0 && i + 1 < count)) {
                    action = Action::Down;
                    target = i;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("消す", nullptr, false, i > 0)) {
                    action = Action::Delete;
                    target = i;
                }
                ImGui::EndPopup();
            }
            const float infoWidth = ImGui::CalcTextSize(info.c_str()).x;
            ImGui::SameLine(startX + (std::max)(0.0f, available - infoWidth));
            ImGui::TextColored(i == 0 ? Theme::kTextMute : Theme::kTextDim, "%s", info.c_str());
            ImGui::PopID();
        }
        switch (action) {
        case Action::Up: document.MoveLayer(target, -1); break;
        case Action::Down: document.MoveLayer(target, 1); break;
        case Action::Delete: document.DeleteLayer(target); break;
        case Action::None: break;
        }
        ImGui::Spacing();
    }

    void AnimatorLayerPanel::DrawInspector(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        const int index = document.selected;
        if (index < 0 || index >= static_cast<int>(document.data.layers.size())) {
            return;
        }
        UI::SectionHeader("レイヤー");
        {
            std::string newName;
            if (AnimatorWidgets::NameField(document, "名前", document.data.layers[static_cast<size_t>(index)].name, newName)) {
                document.RenameLayer(index, newName);
            }
        }
        AnimatorLayer& layer = document.data.layers[static_cast<size_t>(index)];
        ImGui::PushTextWrapPos(0.0f);
        if (index == 0) {
            UI::Hint("最初のレイヤーは重み 1 で全部の骨を動かします。2 枚目からは、この姿勢へ重みとマスクの分だけ上書きします。");
        } else {
            float weight = layer.weight;
            if (ImGui::SliderFloat("重み", &weight, 0.0f, 1.0f, "%.2f")) {
                layer.weight = weight;
                document.MarkChanged("レイヤーの重みを変える");
            }
            UI::Tooltip("0 なら重ねず、1 ならマスクの骨をこのレイヤーの姿勢で上書きします。"
                "スクリプトからは animator.SetLayerWeight(\"名前\", 値) で変えます。");
            if (live.runtime) {
                if (const int running = live.runtime->FindLayer(layer.name); running >= 0) {
                    float liveWeight = live.runtime->GetLayerWeight(running);
                    if (ImGui::SliderFloat("再生中の重み", &liveWeight, 0.0f, 1.0f, "%.2f")) {
                        live.runtime->SetLayerWeight(running, liveWeight);
                    }
                    UI::Tooltip("動いているオブジェクトの重みです。止めると設定の値へ戻ります。");
                }
            }

            bool useMask = layer.useMask;
            if (ImGui::Checkbox("体の一部だけ動かす（マスク）", &useMask)) {
                layer.useMask = useMask;
                if (useMask && layer.mask.empty()) {
                    if (const Skeleton* const skeleton = AnimatorWidgets::BoneTreeOf(document)) {
                        for (const Joint& joint : skeleton->joints) {
                            layer.mask.push_back(joint.name);
                        }
                    }
                }
                document.MarkChanged("マスクを変える");
            }
            UI::Tooltip("チェックした骨だけをこのレイヤーで動かします。外した骨は下のレイヤーの姿勢のままです。");
            if (layer.useMask) {
                DrawMask(document, index);
            }
        }

        ImGui::Spacing();
        ImGui::TextWrapped("状態 %zu 個・遷移 %zu 本", layer.states.size(), layer.transitions.size());
        if (index == document.activeLayer) {
            UI::Hint("キャンバスはこのレイヤーの状態と遷移を出しています。");
        }
        if (index > 0) {
            UI::Hint("流すものの無い状態（クリップが空）では、下のレイヤーの姿勢のままになります。");
        }
        ImGui::PopTextWrapPos();
    }

    void AnimatorLayerPanel::DrawMask(AnimatorDocument& document, int layerIndex)
    {
        AnimatorLayer& layer = document.data.layers[static_cast<size_t>(layerIndex)];
        const Skeleton* const skeleton = AnimatorWidgets::BoneTreeOf(document);
        if (!skeleton) {
            UI::Hint("骨の木を出すには、設定の「見本のモデル」に骨入りのモデルを入れてください。");
            ImGui::Text("動かす骨 %zu 本", layer.mask.size());
            return;
        }

        const size_t jointCount = skeleton->joints.size();
        std::vector<std::vector<int>> children(jointCount);
        for (size_t i = 0; i < jointCount; ++i) {
            if (const std::optional<int32_t>& parent = skeleton->joints[i].parent; parent && static_cast<size_t>(*parent) < jointCount) {
                children[static_cast<size_t>(*parent)].push_back(static_cast<int>(i));
            }
        }

        std::unordered_set<std::string> enabled(layer.mask.begin(), layer.mask.end());
        bool changed = false;
        if (ImGui::Button("全部")) {
            for (const Joint& joint : skeleton->joints) {
                enabled.insert(joint.name);
            }
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("外す")) {
            enabled.clear();
            changed = true;
        }
        ImGui::SameLine();
        const size_t on = static_cast<size_t>(std::count_if(skeleton->joints.begin(), skeleton->joints.end(),
            [&](const Joint& joint) { return enabled.contains(joint.name); }));
        ImGui::TextDisabled("動かす骨 %zu / %zu", on, jointCount);

        for (size_t i = 0; i < jointCount; ++i) {
            if (!skeleton->joints[i].parent) {
                changed |= DrawBone(*skeleton, children, static_cast<int>(i), enabled);
            }
        }
        if (!changed) {
            return;
        }

        // 骨の木の順に並べ、このモデルに無い名前は元の順のまま後ろへ残す
        std::vector<std::string> mask;
        for (const Joint& joint : skeleton->joints) {
            if (enabled.erase(joint.name) > 0) {
                mask.push_back(joint.name);
            }
        }
        for (const std::string& bone : layer.mask) {
            if (enabled.contains(bone)) {
                mask.push_back(bone);
            }
        }
        layer.mask = std::move(mask);
        document.MarkChanged("マスクを変える");
    }

    bool AnimatorLayerPanel::DrawBone(const Skeleton& skeleton, const std::vector<std::vector<int>>& children, int joint,
        std::unordered_set<std::string>& enabled)
    {
        const Joint& bone = skeleton.joints[static_cast<size_t>(joint)];
        const std::vector<int>& kids = children[static_cast<size_t>(joint)];
        ImGui::PushID(joint);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;
        if (kids.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        const bool open = ImGui::TreeNodeEx("##bone", flags);
        ImGui::SameLine();
        bool checked = enabled.contains(bone.name);
        bool changed = false;
        if (ImGui::Checkbox(bone.name.c_str(), &checked)) {
            SetSubtree(skeleton, children, joint, checked, enabled);
            changed = true;
        }
        if (open && !kids.empty()) {
            for (const int child : kids) {
                changed |= DrawBone(skeleton, children, child, enabled);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        return changed;
    }
}

#endif // CORE_EDITOR
