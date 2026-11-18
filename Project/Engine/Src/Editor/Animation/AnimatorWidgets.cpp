#include "pch.h"
#include "AnimatorWidgets.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"
#include "AnimatorPreview.h"

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/DragDropPayload.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "GameObject/Component/Animation/AnimatorComponent.h"
#include "GameObject/Component/Render/SpriteAnimatorComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/Skeleton/Skeleton.h"

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

    bool ClipFields(AnimatorDocument& document, int clipIndex, bool allowDelete, std::optional<float> playhead)
    {
        AnimatorClip& clip = document.data.clips[static_cast<size_t>(clipIndex)];
        std::string newName;
        if (NameField(document, "名前", clip.id, newName)) {
            document.RenameClip(clipIndex, newName);
        }
        const SpriteAnimatorComponent* const sprite = AnimatorDocument::SelectedSprite();
        const bool isSprite = clip.IsSprite();
        if (ImGui::BeginCombo("種類", isSprite ? "スプライトのクリップ" : "モデルのクリップ")) {
            if (ImGui::Selectable("モデルのクリップ", !isSprite) && isSprite) {
                clip.sprite.clear();
                clip.file = document.data.previewModel;
                clip.source.clear();
                document.MarkChanged("クリップの種類を変える");
            }
            if (ImGui::Selectable("スプライトのクリップ", isSprite) && !isSprite) {
                const SpriteAnimationClip* const first = sprite && !sprite->GetClips().empty() ? &sprite->GetClips().front() : nullptr;
                clip.sprite = sprite && sprite->FindClip(clip.id) ? clip.id : (first && !first->name.empty() ? first->name : clip.id);
                clip.file.clear();
                clip.source.clear();
                document.MarkChanged("クリップの種類を変える");
            }
            ImGui::EndCombo();
        }
        if (clip.IsSprite()) {
            SpriteClipFields(document, clip, sprite);
        } else {
            ModelClipFields(document, clip);
        }
        ClipEvents(document, clipIndex, playhead);
        if (!allowDelete) {
            return false;
        }
        if (ImGui::SmallButton("このクリップを消す")) {
            document.DeleteClip(clipIndex);
            return true;
        }
        return false;
    }

    void ModelClipFields(AnimatorDocument& document, AnimatorClip& clip)
    {
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
    }

    void SpriteClipFields(AnimatorDocument& document, AnimatorClip& clip, const SpriteAnimatorComponent* sprite)
    {
        if (sprite && !sprite->GetClips().empty()) {
            if (ImGui::BeginCombo("スプライトのクリップ", clip.sprite.c_str())) {
                for (const SpriteAnimationClip& candidate : sprite->GetClips()) {
                    if (ImGui::Selectable(candidate.name.c_str(), candidate.name == clip.sprite)) {
                        clip.sprite = candidate.name;
                        document.MarkChanged("スプライトのクリップを選ぶ");
                    }
                }
                ImGui::EndCombo();
            }
            if (!sprite->FindClip(clip.sprite)) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(Theme::kWarn, "選んでいるオブジェクトのスプライトアニメに「%s」がありません", clip.sprite.c_str());
                ImGui::PopTextWrapPos();
            }
        } else {
            std::string newName;
            if (NameField(document, "スプライトのクリップ", clip.sprite, newName) && !newName.empty()) {
                clip.sprite = newName;
                document.MarkChanged("スプライトのクリップを選ぶ");
            }
            ImGui::PushTextWrapPos(0.0f);
            UI::Hint("Hierarchy でスプライトアニメのあるオブジェクトを選ぶと、クリップの候補と尺が出ます。");
            ImGui::PopTextWrapPos();
        }
    }

    void ClipEvents(AnimatorDocument& document, int clipIndex, std::optional<float> playhead)
    {
        AnimatorClip& clip = document.data.clips[static_cast<size_t>(clipIndex)];
        const float duration = document.ClipDurationOf(clip);
        ImGui::PushID("events");
        ImGui::TextUnformatted("イベント");
        if (duration > 0.0f) {
            ImGui::SameLine();
            ImGui::TextDisabled("（尺 %.3f 秒）", duration);
        }

        // 帯：左端が 0 秒・右端が尺。印をドラッグで動かし、空いた所のダブルクリックで足す
        const float width = std::max(60.0f, ImGui::GetContentRegionAvail().x);
        const float height = ImGui::GetFrameHeight() * 1.2f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##timeline", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const bool activated = ImGui::IsItemActivated();
        const bool deactivated = ImGui::IsItemDeactivated();
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const ImVec2 end(origin.x + width, origin.y + height);
        drawList->AddRectFilled(origin, end, ImGui::GetColorU32(Theme::kDeepest), 3.0f);
        drawList->AddRect(origin, end, ImGui::GetColorU32(Theme::WithAlpha(Theme::kTextDim, 0.35f)), 3.0f);
        const auto toX = [&](float seconds) {
            return origin.x + (duration > 0.0f ? std::clamp(seconds / duration, 0.0f, 1.0f) : 0.0f) * width;
            };
        const auto toSeconds = [&](float x) {
            return duration > 0.0f ? std::clamp((x - origin.x) / width, 0.0f, 1.0f) * duration : 0.0f;
            };
        const float mouseX = ImGui::GetIO().MousePos.x;

        int nearest = -1;
        float nearestDistance = 6.0f;
        for (size_t i = 0; i < clip.events.size(); ++i) {
            const float distance = std::fabs(toX(clip.events[i].time) - mouseX);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = static_cast<int>(i);
            }
        }
        if (activated) {
            document.eventDragClip = nearest >= 0 ? clipIndex : -1;
            document.eventDragIndex = nearest;
        }
        const bool dragging = active && document.eventDragClip == clipIndex && document.eventDragIndex >= 0 &&
            document.eventDragIndex < static_cast<int>(clip.events.size());
        if (dragging && duration > 0.0f) {
            float& time = clip.events[static_cast<size_t>(document.eventDragIndex)].time;
            const float moved = toSeconds(mouseX);
            if (moved != time) {
                time = moved;
                document.MarkChanged("イベントの時刻を変える");
            }
        }
        if (deactivated) {
            document.eventDragClip = -1;
            document.eventDragIndex = -1;
        }
        if (hovered && nearest < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            AnimatorClipEvent added;
            added.time = toSeconds(mouseX);
            added.name = "Event";
            clip.events.push_back(std::move(added));
            document.MarkChanged("イベントを足す");
        }

        const int hot = dragging ? document.eventDragIndex : nearest;
        for (size_t i = 0; i < clip.events.size(); ++i) {
            const float x = toX(clip.events[i].time);
            const bool isHot = static_cast<int>(i) == hot;
            const ImU32 color = ImGui::GetColorU32(isHot ? Theme::kAccentHover : Theme::kWarm);
            drawList->AddLine(ImVec2(x, origin.y + 2.0f), ImVec2(x, end.y - 2.0f), color, isHot ? 2.0f : 1.5f);
            drawList->AddTriangleFilled(ImVec2(x - 4.0f, origin.y + 1.0f), ImVec2(x + 4.0f, origin.y + 1.0f), ImVec2(x, origin.y + 7.0f), color);
        }
        if (playhead && duration > 0.0f) {
            const float x = toX(*playhead);
            drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), ImGui::GetColorU32(Theme::kOk), 1.5f);
        }
        if (hovered && !dragging) {
            if (nearest >= 0) {
                const AnimatorClipEvent& event = clip.events[static_cast<size_t>(nearest)];
                ImGui::SetTooltip("%s（%.3f 秒）", event.name.c_str(), event.time);
            } else if (duration > 0.0f) {
                ImGui::SetTooltip("ダブルクリックでイベントを足す（%.3f 秒）", toSeconds(mouseX));
            } else {
                ImGui::SetTooltip("クリップを読めないので、帯に時刻を出せません");
            }
        }

        // 表：時刻・名前・渡す値
        const float deleteWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        for (size_t i = 0; i < clip.events.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            AnimatorClipEvent& event = clip.events[i];
            const float rowWidth = ImGui::GetContentRegionAvail().x - deleteWidth - spacing;
            ImGui::SetNextItemWidth(rowWidth * 0.38f);
            if (ImGui::DragFloat("##time", &event.time, 0.005f, 0.0f, duration > 0.0f ? duration : 1000.0f, "%.3f 秒")) {
                event.time = std::max(0.0f, event.time);
                document.MarkChanged("イベントの時刻を変える");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(rowWidth - rowWidth * 0.38f - spacing);
            if (UI::InputString("##name", event.name)) {
                document.MarkChanged("イベントの名前を変える");
            }
            ImGui::SameLine();
            const bool remove = ImGui::Button("×", ImVec2(deleteWidth, 0.0f));
            UI::Tooltip("このイベントを消す");

            const float valueWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
            ImGui::SetNextItemWidth(valueWidth);
            if (ImGui::DragFloat("##float", &event.floatValue, 0.01f, 0.0f, 0.0f, "数 %g")) {
                document.MarkChanged("イベントの値を変える");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(valueWidth);
            if (ImGui::DragInt("##int", &event.intValue, 0.1f, 0, 0, "整数 %d")) {
                document.MarkChanged("イベントの値を変える");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(valueWidth);
            if (UI::InputStringWithHint("##string", "文字", event.stringValue)) {
                document.MarkChanged("イベントの値を変える");
            }
            ImGui::PopID();
            if (remove) {
                clip.events.erase(clip.events.begin() + static_cast<std::ptrdiff_t>(i));
                document.MarkChanged("イベントを消す");
                break;
            }
        }
        if (ImGui::SmallButton("イベントを足す")) {
            AnimatorClipEvent added;
            added.time = playhead && duration > 0.0f ? std::clamp(*playhead, 0.0f, duration) : 0.0f;
            added.name = "Event";
            clip.events.push_back(std::move(added));
            document.MarkChanged("イベントを足す");
        }
        UI::Tooltip("同じオブジェクトのスクリプトの OnAnimationEvent に、時刻を過ぎたフレームで届きます。");
        ImGui::PopID();
    }

    bool FloatParameterCombo(const char* label, const AnimatorControllerData& data, std::string& parameter)
    {
        bool changed = false;
        if (ImGui::BeginCombo(label, parameter.empty() ? "（選ぶ）" : parameter.c_str())) {
            for (const AnimatorParameter& candidate : data.parameters) {
                if (candidate.type != AnimatorParameterType::Float) {
                    continue;
                }
                if (ImGui::Selectable(candidate.name.c_str(), candidate.name == parameter)) {
                    parameter = candidate.name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        const AnimatorParameter* const found = data.FindParameter(parameter);
        if (parameter.empty()) {
            ImGui::TextColored(Theme::kWarn, "数（float）のパラメータを選んでください（0 として混ぜます）");
        } else if (!found) {
            ImGui::TextColored(Theme::kWarn, "パラメータ「%s」がありません", parameter.c_str());
        } else if (found->type != AnimatorParameterType::Float) {
            ImGui::TextColored(Theme::kWarn, "「%s」は数（float）ではありません", parameter.c_str());
        }
        return changed;
    }

    const Skeleton* BoneTreeOf(AnimatorDocument& document)
    {
        const std::string& model = document.data.previewModel;
        if (document.models && !model.empty()) {
            ModelResource* resource = document.models->GetModelResource(model);
            if (!resource && document.skeletonTried != model) {
                document.skeletonTried = model;
                document.models->PreloadModels({ model });
                resource = document.models->GetModelResource(model);
            }
            if (resource && resource->GetSkeleton()) {
                return &*resource->GetSkeleton();
            }
        }
        SceneDebugEditor* const sceneEditor = SceneAccess::SceneEditor();
        GameObject* const object = sceneEditor ? sceneEditor->GetSelectedObject() : nullptr;
        AnimatorComponent* const animator = object ? object->GetComponent<AnimatorComponent>() : nullptr;
        return animator && AnimatorPreview::UsesDocument(*animator, document) ? animator->GetSkeleton() : nullptr;
    }
}

#endif // CORE_EDITOR
