#include "pch.h"
#include "AnimatorInspectorPanel.h"

#ifdef CORE_EDITOR

#include "AnimatorDocument.h"
#include "AnimatorLiveView.h"
#include "AnimatorPreview.h"
#include "AnimatorWidgets.h"

#include "Animation/AnimatorRuntime.h"
#include "Animation/BlendTree.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/DragDropPayload.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr const char* kMotionLabels[] = { "クリップ 1 本", "1D ブレンド（パラメータ 1 つ）", "2D ブレンド（パラメータ 2 つ）" };

        std::string FirstFloatParameter(const AnimatorControllerData& data, const std::string& skip = {})
        {
            for (const AnimatorParameter& parameter : data.parameters) {
                if (parameter.type == AnimatorParameterType::Float && parameter.name != skip) {
                    return parameter.name;
                }
            }
            return {};
        }

        void SetMotion(AnimatorDocument& document, AnimatorState& state, AnimatorMotionType motion)
        {
            if (state.motion == motion) {
                return;
            }
            state.motion = motion;
            if (motion == AnimatorMotionType::Clip) {
                if (state.clip.empty() && !state.blend.children.empty()) {
                    state.clip = state.blend.children.front().clip;
                }
            } else {
                if (state.blend.children.empty()) {
                    AnimatorBlendChild first;
                    first.clip = state.clip;
                    state.blend.children.push_back(std::move(first));
                }
                if (state.blend.parameterX.empty()) {
                    state.blend.parameterX = FirstFloatParameter(document.data);
                }
                if (motion == AnimatorMotionType::Blend2D && state.blend.parameterY.empty()) {
                    state.blend.parameterY = FirstFloatParameter(document.data, state.blend.parameterX);
                }
            }
            document.blendChildFocus = 0;
            document.MarkChanged("状態の流すものを変える");
        }

        /// @brief 図に出す値（再生中は動いている値、停止中はシーンで見る値）
        float BlendValue(const AnimatorDocument& document, const AnimatorLiveView& live, const std::string& name)
        {
            if (live.runtime && live.runtime->GetController()) {
                const AnimatorParameter* const running = live.runtime->GetController()->FindParameter(name);
                if (running && running->type == AnimatorParameterType::Float) {
                    return live.runtime->GetFloat(name);
                }
            }
            return document.PreviewFloat(name);
        }

        void WriteBlendValue(AnimatorDocument& document, const AnimatorLiveView& live, const std::string& name, float value)
        {
            if (name.empty()) {
                return;
            }
            if (live.runtime && live.runtime->GetController()) {
                const AnimatorParameter* const running = live.runtime->GetController()->FindParameter(name);
                if (running && running->type == AnimatorParameterType::Float) {
                    live.runtime->SetFloat(name, value);
                }
                return;
            }
            document.previewFloats[name] = value;
        }

        /// @brief 子の重み（クリップの表に無い子は 0 にして足して 1 へ直す）
        std::vector<float> BlendWeights(const AnimatorControllerData& data, const AnimatorState& state, const Vector2& value)
        {
            const size_t count = state.blend.children.size();
            std::vector<float> weights(count, 0.0f);
            if (count == 0) {
                return weights;
            }
            if (state.motion == AnimatorMotionType::Blend1D) {
                std::vector<float> thresholds(count);
                for (size_t i = 0; i < count; ++i) {
                    thresholds[i] = state.blend.children[i].threshold;
                }
                BlendTree::Compute1D(thresholds, value.x, weights);
            } else {
                std::vector<Vector2> positions(count);
                for (size_t i = 0; i < count; ++i) {
                    positions[i] = state.blend.children[i].position;
                }
                BlendTree::Compute2D(positions, value, weights);
            }
            float total = 0.0f;
            for (size_t i = 0; i < count; ++i) {
                if (!data.FindClip(state.blend.children[i].clip)) {
                    weights[i] = 0.0f;
                }
                total += weights[i];
            }
            if (total > 0.0f) {
                for (float& weight : weights) {
                    weight /= total;
                }
            }
            return weights;
        }

        /// @brief 状態の進みの割合（再生中か、シーンで見ている状態なら）
        std::optional<float> NormalizedOf(const AnimatorDocument& document, const AnimatorState& state, const AnimatorLiveView& live)
        {
            if (live.runtime && live.currentState == state.name) {
                return live.normalizedTime;
            }
            if (document.previewEnabled && !document.previewObject.empty() && AnimatorPreview::PreviewStateOf(document) == state.name) {
                return document.previewTime;
            }
            return std::nullopt;
        }

        ImU32 ToU32(const ImVec4& color)
        {
            return ImGui::GetColorU32(color);
        }

        /// @brief 1D の図（横の線に子の境の値・下に重みの棒・値の印）。ドラッグで値を動かす
        std::optional<float> DrawBlendLine(const AnimatorState& state, const std::vector<float>& weights, float value, bool liveValue)
        {
            float low = value;
            float high = value;
            for (const AnimatorBlendChild& child : state.blend.children) {
                low = std::min(low, child.threshold);
                high = std::max(high, child.threshold);
            }
            if (high - low < 1e-4f) {
                low -= 1.0f;
                high += 1.0f;
            }
            const float pad = (high - low) * 0.08f;
            low -= pad;
            high += pad;

            const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x);
            const float height = ImGui::GetFrameHeight() * 2.6f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##blend1d", ImVec2(width, height));
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered() && !active) {
                ImGui::SetTooltip(liveValue ? "ドラッグで再生中の値を動かす" : "ドラッグで値を動かして混ざり方を見る（保存はしない）");
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 end(origin.x + width, origin.y + height);
            drawList->AddRectFilled(origin, end, ToU32(Theme::kDeepest), 3.0f);
            const auto toX = [&](float v) { return origin.x + (v - low) / (high - low) * width; };
            const float labelHeight = ImGui::GetTextLineHeight();
            const float axisY = origin.y + labelHeight + 6.0f;
            drawList->AddLine(ImVec2(origin.x + 4.0f, axisY), ImVec2(end.x - 4.0f, axisY), ToU32(Theme::WithAlpha(Theme::kTextDim, 0.5f)), 1.0f);
            const float barMax = end.y - axisY - 6.0f;
            for (size_t i = 0; i < state.blend.children.size(); ++i) {
                const AnimatorBlendChild& child = state.blend.children[i];
                const float x = toX(child.threshold);
                const float weight = i < weights.size() ? weights[i] : 0.0f;
                const ImU32 color = ToU32(Theme::WithAlpha(Theme::kWarm, 0.45f + 0.55f * weight));
                drawList->AddLine(ImVec2(x, axisY - 4.0f), ImVec2(x, axisY + 4.0f), color, 2.0f);
                if (weight > 0.0f) {
                    drawList->AddRectFilled(ImVec2(x - 4.0f, axisY + 4.0f), ImVec2(x + 4.0f, axisY + 4.0f + barMax * weight), color, 1.0f);
                }
                const std::string label = child.clip.empty() ? std::string("（なし）") : child.clip;
                const float textWidth = ImGui::CalcTextSize(label.c_str()).x;
                const float textX = std::clamp(x - textWidth * 0.5f, origin.x + 2.0f, std::max(origin.x + 2.0f, end.x - textWidth - 2.0f));
                drawList->AddText(ImVec2(textX, origin.y + 2.0f), ToU32(weight > 0.0f ? Theme::kText : Theme::kTextDim), label.c_str());
            }
            const float valueX = std::clamp(toX(value), origin.x, end.x);
            const ImU32 valueColor = ToU32(liveValue ? Theme::kOk : Theme::kAccentHover);
            drawList->AddLine(ImVec2(valueX, axisY - 6.0f), ImVec2(valueX, end.y - 2.0f), valueColor, 2.0f);
            drawList->AddCircleFilled(ImVec2(valueX, axisY), 4.0f, valueColor);
            if (active) {
                const float mouseX = ImGui::GetIO().MousePos.x;
                return low + std::clamp((mouseX - origin.x) / width, 0.0f, 1.0f) * (high - low);
            }
            return std::nullopt;
        }

        /// @brief 2D の図（平面に子の点・重みで大きさ・値の印）。ドラッグで値を動かす
        std::optional<Vector2> DrawBlendPlane(const AnimatorState& state, const std::vector<float>& weights, const Vector2& value, bool liveValue)
        {
            float extent = std::max({ 1.0f, std::fabs(value.x), std::fabs(value.y) });
            for (const AnimatorBlendChild& child : state.blend.children) {
                extent = std::max({ extent, std::fabs(child.position.x), std::fabs(child.position.y) });
            }
            extent *= 1.15f;

            const float size = std::clamp(ImGui::GetContentRegionAvail().x, 80.0f, 260.0f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##blend2d", ImVec2(size, size));
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered() && !active) {
                ImGui::SetTooltip(liveValue ? "ドラッグで再生中の値を動かす" : "ドラッグで値を動かして混ざり方を見る（保存はしない）");
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 end(origin.x + size, origin.y + size);
            const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
            const float scale = size * 0.5f / extent;
            const auto toScreen = [&](const Vector2& p) { return ImVec2(center.x + p.x * scale, center.y - p.y * scale); };
            drawList->AddRectFilled(origin, end, ToU32(Theme::kDeepest), 3.0f);
            const ImU32 axisColor = ToU32(Theme::WithAlpha(Theme::kTextDim, 0.35f));
            drawList->AddLine(ImVec2(origin.x, center.y), ImVec2(end.x, center.y), axisColor, 1.0f);
            drawList->AddLine(ImVec2(center.x, origin.y), ImVec2(center.x, end.y), axisColor, 1.0f);
            drawList->AddCircle(center, scale, axisColor, 48, 1.0f);

            for (size_t i = 0; i < state.blend.children.size(); ++i) {
                const AnimatorBlendChild& child = state.blend.children[i];
                const ImVec2 point = toScreen(child.position);
                const float weight = i < weights.size() ? weights[i] : 0.0f;
                const ImU32 color = ToU32(Theme::WithAlpha(Theme::kWarm, 0.45f + 0.55f * weight));
                drawList->AddCircleFilled(point, 3.0f + 8.0f * weight, color);
                const std::string label = child.clip.empty() ? std::string("（なし）") : child.clip;
                drawList->AddText(ImVec2(point.x + 6.0f, point.y - ImGui::GetTextLineHeight()), ToU32(weight > 0.0f ? Theme::kText : Theme::kTextDim), label.c_str());
            }
            const ImVec2 valuePoint = toScreen(value);
            const ImU32 valueColor = ToU32(liveValue ? Theme::kOk : Theme::kAccentHover);
            drawList->AddCircle(valuePoint, 6.0f, valueColor, 16, 2.0f);
            drawList->AddLine(ImVec2(valuePoint.x - 9.0f, valuePoint.y), ImVec2(valuePoint.x + 9.0f, valuePoint.y), valueColor, 1.5f);
            drawList->AddLine(ImVec2(valuePoint.x, valuePoint.y - 9.0f), ImVec2(valuePoint.x, valuePoint.y + 9.0f), valueColor, 1.5f);
            if (active) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float x = std::clamp((std::clamp(mouse.x, origin.x, end.x) - center.x) / scale, -extent, extent);
                const float y = std::clamp((center.y - std::clamp(mouse.y, origin.y, end.y)) / scale, -extent, extent);
                return Vector2{ x, y };
            }
            return std::nullopt;
        }

        /// @brief クリップを選ぶ欄（（なし）も選べる）
        bool ClipCombo(const char* label, const AnimatorControllerData& data, std::string& clip)
        {
            bool changed = false;
            if (ImGui::BeginCombo(label, clip.empty() ? "（なし）" : clip.c_str())) {
                if (ImGui::Selectable("（なし）", clip.empty())) {
                    clip.clear();
                    changed = true;
                }
                for (const AnimatorClip& candidate : data.clips) {
                    if (ImGui::Selectable(candidate.id.c_str(), candidate.id == clip)) {
                        clip = candidate.id;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }

    void AnimatorInspectorPanel::Draw(AnimatorDocument& document, const AnimatorLiveView& live)
    {
        const AnimatorLayer& layer = document.data.layers.front();
        switch (document.selection) {
        case AnimatorSelection::State:
            if (document.selected >= 0 && document.selected < static_cast<int>(layer.states.size())) {
                DrawStateInspector(document, document.selected, live);
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

    void AnimatorInspectorPanel::DrawStateInspector(AnimatorDocument& document, int stateIndex, const AnimatorLiveView& live)
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

        int motion = static_cast<int>(state.motion);
        if (ImGui::Combo("流すもの", &motion, kMotionLabels, static_cast<int>(std::size(kMotionLabels)))) {
            SetMotion(document, state, static_cast<AnimatorMotionType>(motion));
        }
        UI::Tooltip("ブレンドツリーは、パラメータの値で複数のクリップを混ぜます（歩きと走りを速さで混ぜる、など）。");
        if (state.motion == AnimatorMotionType::Clip) {
            DrawClipMotion(document, stateIndex, live);
        } else {
            DrawBlendTree(document, stateIndex, live);
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

    void AnimatorInspectorPanel::DrawClipMotion(AnimatorDocument& document, int stateIndex, const AnimatorLiveView& live)
    {
        AnimatorState& state = document.data.layers.front().states[static_cast<size_t>(stateIndex)];
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
            const std::optional<float> normalized = NormalizedOf(document, state, live);
            std::optional<float> playhead;
            if (normalized) {
                playhead = *normalized * document.ClipDurationOf(*clip);
            }
            ImGui::Indent();
            ImGui::PushID("clip");
            AnimatorWidgets::ClipFields(document, clipIndex, false, playhead);
            ImGui::PopID();
            ImGui::Unindent();
        }
    }

    void AnimatorInspectorPanel::DrawBlendTree(AnimatorDocument& document, int stateIndex, const AnimatorLiveView& live)
    {
        AnimatorState& state = document.data.layers.front().states[static_cast<size_t>(stateIndex)];
        AnimatorBlendTree& tree = state.blend;
        const bool is2D = state.motion == AnimatorMotionType::Blend2D;
        if (AnimatorWidgets::FloatParameterCombo(is2D ? "横のパラメータ" : "パラメータ", document.data, tree.parameterX)) {
            document.MarkChanged("ブレンドツリーのパラメータを選ぶ");
        }
        if (is2D && AnimatorWidgets::FloatParameterCombo("縦のパラメータ", document.data, tree.parameterY)) {
            document.MarkChanged("ブレンドツリーのパラメータを選ぶ");
        }

        const bool liveValue = live.runtime != nullptr;
        const Vector2 value{ BlendValue(document, live, tree.parameterX), is2D ? BlendValue(document, live, tree.parameterY) : 0.0f };
        const std::vector<float> weights = BlendWeights(document.data, state, value);
        if (is2D) {
            ImGui::Text("値 (%.2f, %.2f)%s", value.x, value.y, liveValue ? "（再生中）" : "");
            if (const std::optional<Vector2> moved = DrawBlendPlane(state, weights, value, liveValue)) {
                WriteBlendValue(document, live, tree.parameterX, moved->x);
                WriteBlendValue(document, live, tree.parameterY, moved->y);
            }
        } else {
            ImGui::Text("値 %.2f%s", value.x, liveValue ? "（再生中）" : "");
            if (const std::optional<float> moved = DrawBlendLine(state, weights, value.x, liveValue)) {
                WriteBlendValue(document, live, tree.parameterX, *moved);
            }
        }
        ImGui::PushTextWrapPos(0.0f);
        UI::Hint(is2D
            ? "子を平面に置き、値の向きと長さで近い子を混ぜます。原点に止まっている姿勢、周りに向きごとの動きを置きます。"
            : "値をはさむ隣の 2 本を混ぜます。子は進みの割合をそろえて流れます。");
        ImGui::PopTextWrapPos();

        UI::SectionHeader("子");
        const float deleteWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        document.blendChildFocus = std::clamp(document.blendChildFocus, 0, std::max(0, static_cast<int>(tree.children.size()) - 1));
        for (size_t i = 0; i < tree.children.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            AnimatorBlendChild& child = tree.children[i];
            if (ImGui::RadioButton("##focus", document.blendChildFocus == static_cast<int>(i))) {
                document.blendChildFocus = static_cast<int>(i);
            }
            UI::Tooltip("下にこの子のクリップの設定（イベント）を出す");
            ImGui::SameLine();
            const float weightWidth = ImGui::CalcTextSize("100%").x;
            const float rowWidth = ImGui::GetContentRegionAvail().x - deleteWidth - weightWidth - spacing * 2.0f;
            ImGui::SetNextItemWidth(rowWidth * (is2D ? 0.42f : 0.55f));
            if (ClipCombo("##clip", document.data, child.clip)) {
                document.MarkChanged("ブレンドツリーの子のクリップを選ぶ");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(30.0f, rowWidth * (is2D ? 0.58f : 0.45f) - spacing));
            if (is2D) {
                if (ImGui::DragFloat2("##position", &child.position.x, 0.01f, 0.0f, 0.0f, "%.2f")) {
                    document.MarkChanged("ブレンドツリーの子の位置を変える");
                }
                UI::Tooltip("平面の位置（横・縦のパラメータの値）");
            } else {
                if (ImGui::DragFloat("##threshold", &child.threshold, 0.01f, 0.0f, 0.0f, "%.2f")) {
                    document.MarkChanged("ブレンドツリーの子の境の値を変える");
                }
                UI::Tooltip("パラメータがこの値のとき、この子だけになる");
            }
            ImGui::SameLine();
            const float weight = i < weights.size() ? weights[i] : 0.0f;
            ImGui::TextColored(weight > 0.0f ? Theme::kWarm : Theme::kTextDim, "%3.0f%%", weight * 100.0f);
            ImGui::SameLine();
            const bool remove = ImGui::Button("×", ImVec2(deleteWidth, 0.0f));
            UI::Tooltip("この子を外す");
            if (!child.clip.empty() && !document.data.FindClip(child.clip)) {
                ImGui::TextColored(Theme::kWarn, "クリップ「%s」がありません", child.clip.c_str());
            }
            ImGui::PopID();
            if (remove) {
                tree.children.erase(tree.children.begin() + static_cast<std::ptrdiff_t>(i));
                document.MarkChanged("ブレンドツリーの子を外す");
                break;
            }
        }

        const auto addChild = [&](const std::string& clipId) {
            AnimatorBlendChild child;
            child.clip = clipId;
            if (is2D) {
                const size_t ring = tree.children.size();
                if (ring > 0) {
                    const float angle = static_cast<float>(ring - 1) * 1.5707964f;
                    child.position = Vector2{ std::round(std::cos(angle) * 100.0f) / 100.0f, std::round(std::sin(angle) * 100.0f) / 100.0f };
                }
            } else if (!tree.children.empty()) {
                float highest = tree.children.front().threshold;
                for (const AnimatorBlendChild& existing : tree.children) {
                    highest = std::max(highest, existing.threshold);
                }
                child.threshold = highest + 1.0f;
            }
            tree.children.push_back(std::move(child));
            document.blendChildFocus = static_cast<int>(tree.children.size()) - 1;
            document.MarkChanged("ブレンドツリーの子を足す");
            };
        if (ImGui::Button("子を足す")) {
            addChild(document.data.clips.empty() ? std::string{} : document.data.clips.front().id);
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const std::optional<std::string> dropped = UI::AcceptPayload<std::string>(AnimatorWidgets::kClipPayload)) {
                addChild(*dropped);
            }
            ImGui::EndDragDropTarget();
        }
        UI::Tooltip("左のクリップをここへ落としても足せます。");

        if (tree.children.empty()) {
            return;
        }
        const AnimatorBlendChild& focused = tree.children[static_cast<size_t>(document.blendChildFocus)];
        const AnimatorClip* const clip = document.data.FindClip(focused.clip);
        if (!clip) {
            return;
        }
        ImGui::Spacing();
        ImGui::SeparatorText(("子のクリップ「" + focused.clip + "」").c_str());
        const int clipIndex = static_cast<int>(clip - document.data.clips.data());
        const std::optional<float> normalized = NormalizedOf(document, state, live);
        std::optional<float> playhead;
        if (normalized) {
            playhead = *normalized * document.ClipDurationOf(*clip);
        }
        ImGui::PushID("childclip");
        AnimatorWidgets::ClipFields(document, clipIndex, false, playhead);
        ImGui::PopID();
        ImGui::PushTextWrapPos(0.0f);
        UI::Hint("ブレンドツリーでは、いちばん重い子のクリップのイベントだけが届きます。");
        ImGui::PopTextWrapPos();
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
