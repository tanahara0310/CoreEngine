#include "pch.h"
#include "AnimatorWidgets.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/DragDropPayload.h"
#include "Graphics/Asset/AssetDatabase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace CoreEngine::Editor::AnimatorWidgets
{
    namespace
    {
        constexpr const char* kModelPayload = "MODEL_FILE";

        std::string FormatNumber(float value, bool integer)
        {
            char buffer[32];
            if (integer) {
                std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
            } else {
                std::snprintf(buffer, sizeof(buffer), "%g", value);
            }
            return buffer;
        }

        std::string ConditionText(const AnimatorCondition& condition, const AnimatorControllerData& data)
        {
            const AnimatorParameter* const parameter = data.FindParameter(condition.parameter);
            const bool integer = parameter && parameter->type == AnimatorParameterType::Int;
            switch (condition.op) {
            case AnimatorConditionOp::Greater:
            case AnimatorConditionOp::Less:
            case AnimatorConditionOp::Equal:
            case AnimatorConditionOp::NotEqual:
                return condition.parameter + " " + OpLabel(condition.op) + " " + FormatNumber(condition.value, integer);
            case AnimatorConditionOp::True: return condition.parameter;
            case AnimatorConditionOp::False: return "!" + condition.parameter;
            case AnimatorConditionOp::Trigger: return condition.parameter;
            }
            return condition.parameter;
        }
    }

    const char* TypeGlyph(AnimatorParameterType type)
    {
        switch (type) {
        case AnimatorParameterType::Float: return "F";
        case AnimatorParameterType::Int: return "I";
        case AnimatorParameterType::Bool: return "B";
        case AnimatorParameterType::Trigger: return "T";
        }
        return "?";
    }

    const char* TypeLabel(AnimatorParameterType type)
    {
        switch (type) {
        case AnimatorParameterType::Float: return "数（float）";
        case AnimatorParameterType::Int: return "整数（int）";
        case AnimatorParameterType::Bool: return "真偽（bool）";
        case AnimatorParameterType::Trigger: return "トリガー";
        }
        return "";
    }

    ImVec4 TypeColor(AnimatorParameterType type)
    {
        switch (type) {
        case AnimatorParameterType::Float: return Theme::kAccentHover;
        case AnimatorParameterType::Int: return Theme::kOk;
        case AnimatorParameterType::Bool: return Theme::kWarm;
        case AnimatorParameterType::Trigger: return Theme::kScript;
        }
        return Theme::kText;
    }

    const char* OpLabel(AnimatorConditionOp op)
    {
        switch (op) {
        case AnimatorConditionOp::Greater: return ">";
        case AnimatorConditionOp::Less: return "<";
        case AnimatorConditionOp::Equal: return "==";
        case AnimatorConditionOp::NotEqual: return "!=";
        case AnimatorConditionOp::True: return "true";
        case AnimatorConditionOp::False: return "false";
        case AnimatorConditionOp::Trigger: return "立った";
        }
        return "?";
    }

    bool OpHasValue(AnimatorConditionOp op)
    {
        return op == AnimatorConditionOp::Greater || op == AnimatorConditionOp::Less
            || op == AnimatorConditionOp::Equal || op == AnimatorConditionOp::NotEqual;
    }

    AnimatorConditionOp FirstOp(AnimatorParameterType type)
    {
        for (AnimatorConditionOp op : kAllOps) {
            if (AnimatorControllerFormat::IsValidOp(type, op)) {
                return op;
            }
        }
        return AnimatorConditionOp::Greater;
    }

    std::string TransitionSummary(const AnimatorTransition& transition, const AnimatorControllerData& data)
    {
        std::string text;
        for (const AnimatorCondition& condition : transition.conditions) {
            text += (text.empty() ? "" : " かつ ") + ConditionText(condition, data);
        }
        if (transition.hasExitTime) {
            text += (text.empty() ? "" : "・") + std::string("終わり ") + FormatNumber(transition.exitTime, false);
        }
        return text.empty() ? std::string("すぐ") : text;
    }

    bool NameField(AnimatorDocument& document, const char* label, const std::string& current, std::string& committed)
    {
        const ImGuiID id = ImGui::GetID(label);
        if (document.nameEditId != id) {
            document.nameScratch = current;
        }
        std::string& text = document.nameEditId == id ? document.nameEditText : document.nameScratch;
        UI::InputString(label, text);
        if (ImGui::IsItemActivated()) {
            document.nameEditId = id;
            document.nameEditText = text;
        }
        if (ImGui::IsItemDeactivated() && document.nameEditId == id) {
            document.nameEditId = 0;
            if (document.nameEditText != current) {
                committed = document.nameEditText;
                return true;
            }
        }
        return false;
    }

    bool ModelField(const char* label, std::string& assetPath)
    {
        bool changed = false;
        ImGui::PushID(label);
        const float clearWidth = assetPath.empty() ? 0.0f : ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        const std::string shown = assetPath.empty() ? std::string("（Project からモデルを落とす）") : AnimatorFileNameOf(assetPath);
        ImGui::Button(shown.c_str(), ImVec2(std::max(40.0f, ImGui::CalcItemWidth() - clearWidth), 0.0f));
        if (!assetPath.empty()) {
            UI::Tooltip(assetPath.c_str());
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const std::optional<std::string> model = AcceptModelDrop()) {
                if (*model != assetPath) {
                    assetPath = *model;
                    changed = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (!assetPath.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("×", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
                assetPath.clear();
                changed = true;
            }
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return changed;
    }

    std::optional<std::string> AcceptModelDrop()
    {
        const std::optional<std::string> name = UI::AcceptPayload<std::string>(kModelPayload);
        if (!name) {
            return std::nullopt;
        }
        const std::filesystem::path full = AssetDatabase::GetInstance().FindAssetPath(*name, AssetType::Model);
        if (full.empty()) {
            return std::nullopt;
        }
        return AnimatorDocument::ToAssetPath(full);
    }

    bool ClipFields(AnimatorDocument& document, int clipIndex, bool allowDelete)
    {
        AnimatorClip& clip = document.data.clips[static_cast<size_t>(clipIndex)];
        std::string newName;
        if (NameField(document, "名前", clip.id, newName)) {
            document.RenameClip(clipIndex, newName);
        }
        if (ModelField("ファイル", clip.file)) {
            document.MarkChanged("クリップのファイルを変える");
        }
        const std::vector<std::string>& names = document.ClipNamesOf(clip.file);
        const std::string preview = clip.source.empty() ? std::string("（先頭の 1 本）") : clip.source;
        if (ImGui::BeginCombo("中の名前", preview.c_str())) {
            if (ImGui::Selectable("（先頭の 1 本）", clip.source.empty())) {
                clip.source.clear();
                document.MarkChanged("クリップを選ぶ");
            }
            for (const std::string& name : names) {
                if (ImGui::Selectable(name.c_str(), clip.source == name)) {
                    clip.source = name;
                    document.MarkChanged("クリップを選ぶ");
                }
            }
            ImGui::EndCombo();
        }
        if (clip.file.empty()) {
            ImGui::TextColored(Theme::kWarn, "ファイルがありません（骨の初期の姿勢になります）");
        } else if (!clip.source.empty() && !names.empty()
            && std::find(names.begin(), names.end(), clip.source) == names.end()) {
            ImGui::TextColored(Theme::kWarn, "ファイルに「%s」がありません", clip.source.c_str());
        }
        if (!allowDelete) {
            return false;
        }
        if (ImGui::SmallButton("このクリップを消す")) {
            document.DeleteClip(clipIndex);
            return true;
        }
        return false;
    }
}

#endif // CORE_EDITOR
