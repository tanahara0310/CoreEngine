#include "pch.h"
#include "Editor/Script/EditorScriptBinding.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Script/EditorGUIState.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Script/Binding/BindingRegistrar.h"

#include <angelscript.h>
#include <imgui.h>
#include <scriptarray/scriptarray.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        /// @brief HelpBox の種類
        enum class MessageType : int
        {
            Info,
            Warning,
            Error,
        };

        /// @brief Canvas の後でなければスクリプトの例外にする
        bool RequireCanvas(const char* function)
        {
            if (!RequireGUI(function)) {
                return false;
            }
            if (FrameState().hasCanvas) {
                return true;
            }
            ThrowScriptException(std::string("EditorGUI::") + function + " は EditorGUI::Canvas の後で呼びます");
            return false;
        }

        std::string DisplayPart(const std::string& label)
        {
            return LabelDisplayPart(label);
        }

        std::string FieldId(const std::string& label)
        {
            return LabelFieldId(label);
        }

        void BeginRow(const std::string& label)
        {
            BeginLabeledRow(label);
        }

        /// @brief 値が変わったことを控える
        bool MarkChanged(bool changed)
        {
            if (changed) {
                FrameState().changed = true;
            }
            return changed;
        }

        /// @brief sRGB の色を ImGui の描画先（リニア）の色にする
        ImVec4 ToLinear(const Vector4& color)
        {
            return SrgbToLinear(color.x, color.y, color.z, color.w);
        }

        ImU32 ToColorU32(const Vector4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(ToLinear(color));
        }

        /// @brief Canvas の左上を原点にした座標を画面の座標にする
        ImVec2 CanvasToScreen(const Vector2& position)
        {
            const GUIFrameState& state = FrameState();
            return ImVec2(state.canvasMin.x + position.x, state.canvasMin.y + position.y);
        }

        /// @brief マウスのボタンの番号が ImGui の範囲にあるか
        bool IsValidMouseButton(int button)
        {
            return button >= 0 && button < ImGuiMouseButton_COUNT;
        }

        /// @brief キーを読んでよいか（このウィンドウが前面で、文字を打っていないとき）
        bool CanReadKeys()
        {
            return ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
        }

        /// @brief 文字列の入力欄の長さを std::string に合わせる
        int ResizeStringCallback(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* const text = static_cast<std::string*>(data->UserData);
                text->resize(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = text->data();
            }
            return 0;
        }

        // ---------------------------------------------------------------- 文字と区切り

        void Label(const std::string& text)
        {
            if (!RequireGUI("Label")) {
                return;
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
        }

        void LabelValue(const std::string& label, const std::string& value)
        {
            if (!RequireGUI("Label")) {
                return;
            }
            BeginRow(label);
            ImGui::TextUnformatted(value.c_str());
        }

        void TextColored(const std::string& text, const Vector4& color)
        {
            if (!RequireGUI("TextColored")) {
                return;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(color));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        void TextDisabled(const std::string& text)
        {
            if (!RequireGUI("TextDisabled")) {
                return;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        void BulletText(const std::string& text)
        {
            if (RequireGUI("BulletText")) {
                ImGui::BulletText("%s", text.c_str());
            }
        }

        void Header(const std::string& text)
        {
            if (!RequireGUI("Header")) {
                return;
            }
            ImGui::SeparatorText(DisplayPart(text).c_str());
        }

        void HelpBox(const std::string& message, int type)
        {
            if (!RequireGUI("HelpBox")) {
                return;
            }
            const ImVec4& accent = type == static_cast<int>(MessageType::Error) ? Theme::kError
                : type == static_cast<int>(MessageType::Warning) ? Theme::kWarn
                : Theme::kTextDim;

            constexpr float kBarWidth = 3.0f;
            const ImVec2 padding(8.0f, 6.0f);
            const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
            const float wrapWidth = (std::max)(1.0f, width - kBarWidth - padding.x * 2.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(message.c_str(), nullptr, false, wrapWidth);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + width, min.y + textSize.y + padding.y * 2.0f);

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(Theme::WithAlpha(accent, 0.14f)), 3.0f);
            drawList->AddRectFilled(min, ImVec2(min.x + kBarWidth, max.y), ImGui::GetColorU32(accent));
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(min.x + kBarWidth + padding.x, min.y + padding.y),
                ImGui::GetColorU32(Theme::kText), message.c_str(), nullptr, wrapWidth);
            ImGui::Dummy(ImVec2(width, max.y - min.y));
        }

        void Tooltip(const std::string& text)
        {
            if (RequireGUI("Tooltip")) {
                ImGui::SetItemTooltip("%s", text.c_str());
            }
        }

        void Separator()
        {
            if (RequireGUI("Separator")) {
                ImGui::Separator();
            }
        }

        void Space(float height)
        {
            if (RequireGUI("Space")) {
                ImGui::Dummy(ImVec2(0.0f, (std::max)(0.0f, height)));
            }
        }

        void SameLine()
        {
            if (RequireGUI("SameLine")) {
                ImGui::SameLine();
            }
        }

        float GetAvailableWidth()
        {
            return RequireGUI("GetAvailableWidth") ? ImGui::GetContentRegionAvail().x : 0.0f;
        }

        void ProgressBar(float fraction, const std::string& overlay)
        {
            WidgetScope widget("ProgressBar");
            if (widget) {
                ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-FLT_MIN, 0.0f),
                    overlay.empty() ? nullptr : overlay.c_str());
            }
        }

        // ---------------------------------------------------------------- 押す・選ぶ

        bool Button(const std::string& label, float width)
        {
            WidgetScope widget("Button");
            if (!widget) {
                return false;
            }
            return ImGui::Button(label.c_str(), ImVec2(width < 0.0f ? -FLT_MIN : width, 0.0f));
        }

        bool Toggle(const std::string& label, bool value)
        {
            WidgetScope widget("Toggle");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            MarkChanged(ImGui::Checkbox(FieldId(label).c_str(), &value));
            return value;
        }

        bool RadioButton(const std::string& label, bool active)
        {
            WidgetScope widget("RadioButton");
            return widget && MarkChanged(ImGui::RadioButton(label.c_str(), active));
        }

        int RadioButtonValue(const std::string& label, int selected, int value)
        {
            WidgetScope widget("RadioButton");
            if (widget && MarkChanged(ImGui::RadioButton(label.c_str(), selected == value))) {
                return value;
            }
            return selected;
        }

        bool Selectable(const std::string& label, bool selected, bool spanAllColumns)
        {
            WidgetScope widget("Selectable");
            if (!widget) {
                return false;
            }
            const ImGuiSelectableFlags flags = spanAllColumns
                ? (ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)
                : ImGuiSelectableFlags_None;
            return MarkChanged(ImGui::Selectable(label.c_str(), selected, flags));
        }

        int ListBox(const std::string& label, int selected, const CScriptArray& items, int visibleRows)
        {
            WidgetScope widget("ListBox");
            if (!widget || !RejectInsideNodeEditor("ListBox")) {
                return selected;
            }
            BeginRow(label);
            const float height = static_cast<float>((std::max)(1, visibleRows)) * ImGui::GetTextLineHeightWithSpacing()
                + ImGui::GetStyle().FramePadding.y * 2.0f;
            if (ImGui::BeginListBox(FieldId(label).c_str(), ImVec2(-FLT_MIN, height))) {
                for (asUINT i = 0; i < items.GetSize(); ++i) {
                    const int index = static_cast<int>(i);
                    ImGui::PushID(index);
                    const auto* const item = static_cast<const std::string*>(items.At(i));
                    if (MarkChanged(ImGui::Selectable(item->c_str(), index == selected))) {
                        selected = index;
                    }
                    ImGui::PopID();
                }
                ImGui::EndListBox();
            }
            return selected;
        }

        int Popup(const std::string& label, int selected, const CScriptArray& options)
        {
            WidgetScope widget("Popup");
            if (!widget) {
                return selected;
            }
            BeginRow(label);
            const int count = static_cast<int>(options.GetSize());
            const auto optionAt = [&options](int index) {
                return static_cast<const std::string*>(options.At(static_cast<asUINT>(index)))->c_str();
                };
            const char* const preview = (selected >= 0 && selected < count) ? optionAt(selected) : "";
            if (ImGui::BeginCombo(FieldId(label).c_str(), preview)) {
                for (int i = 0; i < count; ++i) {
                    ImGui::PushID(i);
                    if (MarkChanged(ImGui::Selectable(optionAt(i), i == selected))) {
                        selected = i;
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            return selected;
        }

        bool Foldout(const std::string& label, bool defaultOpen)
        {
            if (!RequireGUI("Foldout")) {
                return false;
            }
            return ImGui::CollapsingHeader(label.c_str(), defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        }

        // ---------------------------------------------------------------- 値の入力

        int IntField(const std::string& label, int value)
        {
            WidgetScope widget("IntField");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            MarkChanged(ImGui::DragInt(FieldId(label).c_str(), &value, 0.1f));
            return value;
        }

        float FloatField(const std::string& label, float value)
        {
            WidgetScope widget("FloatField");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            MarkChanged(ImGui::DragFloat(FieldId(label).c_str(), &value, 0.01f, 0.0f, 0.0f, "%.3f"));
            return value;
        }

        std::string TextField(const std::string& label, const std::string& value)
        {
            WidgetScope widget("TextField");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            std::string text = value;
            MarkChanged(ImGui::InputText(FieldId(label).c_str(), text.data(), text.capacity() + 1,
                ImGuiInputTextFlags_CallbackResize, &ResizeStringCallback, &text));
            return text;
        }

        std::string TextArea(const std::string& label, const std::string& value, float height)
        {
            WidgetScope widget("TextArea");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            const float areaHeight = height > 0.0f ? height : ImGui::GetTextLineHeight() * 5.0f + ImGui::GetStyle().FramePadding.y * 2.0f;
            std::string text = value;
            MarkChanged(ImGui::InputTextMultiline(FieldId(label).c_str(), text.data(), text.capacity() + 1,
                ImVec2(-FLT_MIN, areaHeight), ImGuiInputTextFlags_CallbackResize, &ResizeStringCallback, &text));
            return text;
        }

        int IntSlider(const std::string& label, int value, int min, int max)
        {
            WidgetScope widget("IntSlider");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            MarkChanged(ImGui::SliderInt(FieldId(label).c_str(), &value, min, max));
            return value;
        }

        float Slider(const std::string& label, float value, float min, float max)
        {
            WidgetScope widget("Slider");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            MarkChanged(ImGui::SliderFloat(FieldId(label).c_str(), &value, min, max, "%.3f"));
            return value;
        }

        Vector2 Vector2Field(const std::string& label, const Vector2& value)
        {
            WidgetScope widget("Vector2Field");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            float components[2] = { value.x, value.y };
            MarkChanged(ImGui::DragFloat2(FieldId(label).c_str(), components, 0.05f, 0.0f, 0.0f, "%.3f"));
            return Vector2{ components[0], components[1] };
        }

        Vector3 Vector3Field(const std::string& label, const Vector3& value)
        {
            WidgetScope widget("Vector3Field");
            if (!widget) {
                return value;
            }
            BeginRow(label);
            float components[3] = { value.x, value.y, value.z };
            MarkChanged(ImGui::DragFloat3(FieldId(label).c_str(), components, 0.05f, 0.0f, 0.0f, "%.3f"));
            return Vector3{ components[0], components[1], components[2] };
        }

        Vector4 ColorField(const std::string& label, const Vector4& value)
        {
            WidgetScope widget("ColorField");
            if (!widget) {
                return value;
            }
            BeginRow(label);

            constexpr const char* kPickerId = "##colorPicker";
            float rgba[4] = { value.x, value.y, value.z, value.w };
            const ImVec2 size((std::max)(1.0f, ImGui::CalcItemWidth()), ImGui::GetFrameHeight());
            ImGui::PushID(FieldId(label).c_str());
            if (ImGui::ColorButton("##swatch", ToLinear(value), ImGuiColorEditFlags_AlphaPreviewHalf, size)) {
                ImGui::OpenPopup(kPickerId);
            }
            if (ImGui::BeginPopup(kPickerId)) {
                MarkChanged(ImGui::ColorPicker4("##picker", rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf));
                ImGui::EndPopup();
            }
            ImGui::PopID();
            return Vector4{ rgba[0], rgba[1], rgba[2], rgba[3] };
        }

        // ---------------------------------------------------------------- 状態

        bool GetEnabled()
        {
            return FrameState().enabled;
        }

        void SetEnabled(bool enabled)
        {
            if (RequireGUI("enabled")) {
                FrameState().enabled = enabled;
            }
        }

        bool GetChanged()
        {
            return FrameState().changed;
        }

        void SetChanged(bool changed)
        {
            if (RequireGUI("changed")) {
                FrameState().changed = changed;
            }
        }

        int GetIndentLevel()
        {
            return FrameState().indentLevel;
        }

        void SetIndent(int level)
        {
            if (RequireGUI("indentLevel")) {
                SetIndentLevel(level);
            }
        }

        void PushIdInt(int id)
        {
            if (RequireGUI("PushID")) {
                ImGui::PushID(id);
                PushGUIScope(GUIScopeKind::Id, true);
            }
        }

        void PushIdString(const std::string& id)
        {
            if (RequireGUI("PushID")) {
                ImGui::PushID(id.c_str());
                PushGUIScope(GUIScopeKind::Id, true);
            }
        }

        void PopId()
        {
            PopGUIScope(GUIScopeKind::Id, "PopID");
        }

        // ---------------------------------------------------------------- 部品とウィンドウの状態

        bool IsItemHovered()
        {
            return RequireGUI("IsItemHovered") && ImGui::IsItemHovered();
        }

        bool IsItemActive()
        {
            return RequireGUI("IsItemActive") && ImGui::IsItemActive();
        }

        bool IsItemClicked(int button)
        {
            return RequireGUI("IsItemClicked") && IsValidMouseButton(button) && ImGui::IsItemClicked(button);
        }

        bool IsItemEdited()
        {
            return RequireGUI("IsItemEdited") && ImGui::IsItemEdited();
        }

        bool IsItemDeactivatedAfterEdit()
        {
            return RequireGUI("IsItemDeactivatedAfterEdit") && ImGui::IsItemDeactivatedAfterEdit();
        }

        bool IsWindowHovered()
        {
            return RequireGUI("IsWindowHovered") && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        }

        bool IsWindowFocused()
        {
            return RequireGUI("IsWindowFocused") && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        }

        // ---------------------------------------------------------------- マウスとキー

        bool IsMouseDown(int button)
        {
            return RequireGUI("IsMouseDown") && IsValidMouseButton(button) && ImGui::IsMouseDown(button);
        }

        bool IsMouseClicked(int button)
        {
            return RequireGUI("IsMouseClicked") && IsValidMouseButton(button) && ImGui::IsMouseClicked(button);
        }

        bool IsMouseDoubleClicked(int button)
        {
            return RequireGUI("IsMouseDoubleClicked") && IsValidMouseButton(button) && ImGui::IsMouseDoubleClicked(button);
        }

        bool IsMouseReleased(int button)
        {
            return RequireGUI("IsMouseReleased") && IsValidMouseButton(button) && ImGui::IsMouseReleased(button);
        }

        bool IsMouseDragging(int button)
        {
            return RequireGUI("IsMouseDragging") && IsValidMouseButton(button) && ImGui::IsMouseDragging(button);
        }

        Vector2 GetMouseDragDelta(int button)
        {
            if (!RequireGUI("GetMouseDragDelta") || !IsValidMouseButton(button)) {
                return Vector2{};
            }
            const ImVec2 delta = ImGui::GetMouseDragDelta(button);
            return Vector2{ delta.x, delta.y };
        }

        float GetMouseWheel()
        {
            return RequireGUI("GetMouseWheel") ? ImGui::GetIO().MouseWheel : 0.0f;
        }

        bool IsKeyPressed(int key, bool repeat)
        {
            if (!RequireGUI("IsKeyPressed") || !CanReadKeys()) {
                return false;
            }
            const ImGuiKey imguiKey = ToImGuiKey(key);
            return imguiKey != ImGuiKey_None && ImGui::IsKeyPressed(imguiKey, repeat);
        }

        bool IsKeyDown(int key)
        {
            if (!RequireGUI("IsKeyDown") || !CanReadKeys()) {
                return false;
            }
            const ImGuiKey imguiKey = ToImGuiKey(key);
            return imguiKey != ImGuiKey_None && ImGui::IsKeyDown(imguiKey);
        }

        bool IsKeyReleased(int key)
        {
            if (!RequireGUI("IsKeyReleased") || !CanReadKeys()) {
                return false;
            }
            const ImGuiKey imguiKey = ToImGuiKey(key);
            return imguiKey != ImGuiKey_None && ImGui::IsKeyReleased(imguiKey);
        }

        bool GetCtrl()
        {
            return RequireGUI("ctrl") && ImGui::GetIO().KeyCtrl;
        }

        bool GetShift()
        {
            return RequireGUI("shift") && ImGui::GetIO().KeyShift;
        }

        bool GetAlt()
        {
            return RequireGUI("alt") && ImGui::GetIO().KeyAlt;
        }

        // ---------------------------------------------------------------- 絵を描く場所

        Vector2 Canvas(float width, float height)
        {
            WidgetScope widget("Canvas");
            if (!widget) {
                return Vector2{};
            }
            GUIFrameState& state = FrameState();
            const float w = (std::max)(1.0f, width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
            const float h = (std::max)(1.0f, height > 0.0f ? height : w);

            ImGui::PushID(state.canvasCount++);
            ImGui::InvisibleButton("##canvas", ImVec2(w, h),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            ImGui::PopID();

            state.hasCanvas = true;
            state.canvasMin = ImGui::GetItemRectMin();
            state.canvasMax = ImGui::GetItemRectMax();
            state.canvasHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(state.canvasMin, state.canvasMax, ImGui::GetColorU32(Theme::kDeepest));
            drawList->AddRect(state.canvasMin, state.canvasMax, ImGui::GetColorU32(Theme::kOutline));
            return Vector2{ w, h };
        }

        void DrawRect(const Vector2& min, const Vector2& max, const Vector4& color)
        {
            if (!RequireCanvas("DrawRect")) {
                return;
            }
            const GUIFrameState& state = FrameState();
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(state.canvasMin, state.canvasMax, true);
            drawList->AddRectFilled(CanvasToScreen(min), CanvasToScreen(max), ToColorU32(color));
            drawList->PopClipRect();
        }

        void DrawRectOutline(const Vector2& min, const Vector2& max, const Vector4& color, float thickness)
        {
            if (!RequireCanvas("DrawRectOutline")) {
                return;
            }
            const GUIFrameState& state = FrameState();
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(state.canvasMin, state.canvasMax, true);
            drawList->AddRect(CanvasToScreen(min), CanvasToScreen(max), ToColorU32(color), 0.0f, 0, thickness);
            drawList->PopClipRect();
        }

        void DrawLine(const Vector2& from, const Vector2& to, const Vector4& color, float thickness)
        {
            if (!RequireCanvas("DrawLine")) {
                return;
            }
            const GUIFrameState& state = FrameState();
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(state.canvasMin, state.canvasMax, true);
            drawList->AddLine(CanvasToScreen(from), CanvasToScreen(to), ToColorU32(color), thickness);
            drawList->PopClipRect();
        }

        void DrawCanvasText(const Vector2& position, const std::string& text, const Vector4& color)
        {
            if (!RequireCanvas("DrawText")) {
                return;
            }
            const GUIFrameState& state = FrameState();
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(state.canvasMin, state.canvasMax, true);
            drawList->AddText(CanvasToScreen(position), ToColorU32(color), text.c_str());
            drawList->PopClipRect();
        }

        bool IsCanvasHovered()
        {
            return RequireCanvas("IsCanvasHovered") && FrameState().canvasHovered;
        }

        Vector2 GetCanvasMousePosition()
        {
            if (!RequireCanvas("GetCanvasMousePosition")) {
                return Vector2{};
            }
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const GUIFrameState& state = FrameState();
            return Vector2{ mouse.x - state.canvasMin.x, mouse.y - state.canvasMin.y };
        }
    }

    bool RegisterEditorGUI(asIScriptEngine* engine)
    {
        Script::BindingRegistrar r(engine);
        r.Namespace("EditorGUI");

        r.Enum("MessageType");
        r.EnumValue("MessageType", "Info", static_cast<int>(MessageType::Info));
        r.EnumValue("MessageType", "Warning", static_cast<int>(MessageType::Warning));
        r.EnumValue("MessageType", "Error", static_cast<int>(MessageType::Error));

        // 文字と区切り
        r.Function("void Label(const string &in text)", asFUNCTION(Label));
        r.Function("void Label(const string &in label, const string &in value)", asFUNCTION(LabelValue));
        r.Function("void TextColored(const string &in text, const Vector4 &in color)", asFUNCTION(TextColored));
        r.Function("void TextDisabled(const string &in text)", asFUNCTION(TextDisabled));
        r.Function("void BulletText(const string &in text)", asFUNCTION(BulletText));
        r.Function("void Header(const string &in text)", asFUNCTION(Header));
        r.Function("void HelpBox(const string &in message, MessageType type = EditorGUI::MessageType::Info)",
            asFUNCTION(HelpBox));
        r.Function("void Tooltip(const string &in text)", asFUNCTION(Tooltip));
        r.Function("void Separator()", asFUNCTION(Separator));
        r.Function("void Space(float height = 6)", asFUNCTION(Space));
        r.Function("void SameLine()", asFUNCTION(SameLine));
        r.Function("float GetAvailableWidth()", asFUNCTION(GetAvailableWidth));
        r.Function("void ProgressBar(float fraction, const string &in overlay = \"\")", asFUNCTION(ProgressBar));

        // 押す・選ぶ
        r.Function("bool Button(const string &in label, float width = 0)", asFUNCTION(Button));
        r.Function("bool Toggle(const string &in label, bool value)", asFUNCTION(Toggle));
        r.Function("bool RadioButton(const string &in label, bool active)", asFUNCTION(RadioButton));
        r.Function("int RadioButton(const string &in label, int selected, int value)", asFUNCTION(RadioButtonValue));
        r.Function("bool Selectable(const string &in label, bool selected = false, bool spanAllColumns = false)",
            asFUNCTION(Selectable));
        r.Function("int ListBox(const string &in label, int selected, const array<string> &in items, int visibleRows = 6)",
            asFUNCTION(ListBox));
        r.Function("int Popup(const string &in label, int selected, const array<string> &in options)", asFUNCTION(Popup));
        r.Function("bool Foldout(const string &in label, bool defaultOpen = true)", asFUNCTION(Foldout));

        // 値の入力
        r.Function("int IntField(const string &in label, int value)", asFUNCTION(IntField));
        r.Function("float FloatField(const string &in label, float value)", asFUNCTION(FloatField));
        r.Function("string TextField(const string &in label, const string &in value)", asFUNCTION(TextField));
        r.Function("string TextArea(const string &in label, const string &in value, float height = 0)", asFUNCTION(TextArea));
        r.Function("int IntSlider(const string &in label, int value, int min, int max)", asFUNCTION(IntSlider));
        r.Function("float Slider(const string &in label, float value, float min, float max)", asFUNCTION(Slider));
        r.Function("Vector2 Vector2Field(const string &in label, const Vector2 &in value)", asFUNCTION(Vector2Field));
        r.Function("Vector3 Vector3Field(const string &in label, const Vector3 &in value)", asFUNCTION(Vector3Field));
        r.Function("Vector4 ColorField(const string &in label, const Vector4 &in value)", asFUNCTION(ColorField));

        // 状態
        r.Function("bool get_enabled() property", asFUNCTION(GetEnabled));
        r.Function("void set_enabled(bool enabled) property", asFUNCTION(SetEnabled));
        r.Function("bool get_changed() property", asFUNCTION(GetChanged));
        r.Function("void set_changed(bool changed) property", asFUNCTION(SetChanged));
        r.Function("int get_indentLevel() property", asFUNCTION(GetIndentLevel));
        r.Function("void set_indentLevel(int level) property", asFUNCTION(SetIndent));
        r.Function("void PushID(int id)", asFUNCTION(PushIdInt));
        r.Function("void PushID(const string &in id)", asFUNCTION(PushIdString));
        r.Function("void PopID()", asFUNCTION(PopId));

        // 部品とウィンドウの状態
        r.Function("bool IsItemHovered()", asFUNCTION(IsItemHovered));
        r.Function("bool IsItemActive()", asFUNCTION(IsItemActive));
        r.Function("bool IsItemClicked(MouseButton button = MouseButton::Left)", asFUNCTION(IsItemClicked));
        r.Function("bool IsItemEdited()", asFUNCTION(IsItemEdited));
        r.Function("bool IsItemDeactivatedAfterEdit()", asFUNCTION(IsItemDeactivatedAfterEdit));
        r.Function("bool IsWindowHovered()", asFUNCTION(IsWindowHovered));
        r.Function("bool IsWindowFocused()", asFUNCTION(IsWindowFocused));

        // マウスとキー
        r.Function("bool IsMouseDown(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseDown));
        r.Function("bool IsMouseClicked(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseClicked));
        r.Function("bool IsMouseDoubleClicked(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseDoubleClicked));
        r.Function("bool IsMouseReleased(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseReleased));
        r.Function("bool IsMouseDragging(MouseButton button = MouseButton::Left)", asFUNCTION(IsMouseDragging));
        r.Function("Vector2 GetMouseDragDelta(MouseButton button = MouseButton::Left)", asFUNCTION(GetMouseDragDelta));
        r.Function("float GetMouseWheel()", asFUNCTION(GetMouseWheel));
        r.Function("bool IsKeyPressed(Key key, bool repeat = true)", asFUNCTION(IsKeyPressed));
        r.Function("bool IsKeyDown(Key key)", asFUNCTION(IsKeyDown));
        r.Function("bool IsKeyReleased(Key key)", asFUNCTION(IsKeyReleased));
        r.Function("bool get_ctrl() property", asFUNCTION(GetCtrl));
        r.Function("bool get_shift() property", asFUNCTION(GetShift));
        r.Function("bool get_alt() property", asFUNCTION(GetAlt));

        // 絵を描く場所
        r.Function("Vector2 Canvas(float width, float height = 0)", asFUNCTION(Canvas));
        r.Function("void DrawRect(const Vector2 &in min, const Vector2 &in max, const Vector4 &in color)", asFUNCTION(DrawRect));
        r.Function("void DrawRectOutline(const Vector2 &in min, const Vector2 &in max, const Vector4 &in color, float thickness = 1)",
            asFUNCTION(DrawRectOutline));
        r.Function("void DrawLine(const Vector2 &in from, const Vector2 &in to, const Vector4 &in color, float thickness = 1)",
            asFUNCTION(DrawLine));
        r.Function("void DrawText(const Vector2 &in position, const string &in text, const Vector4 &in color)",
            asFUNCTION(DrawCanvasText));
        r.Function("bool IsCanvasHovered()", asFUNCTION(IsCanvasHovered));
        r.Function("Vector2 GetCanvasMousePosition()", asFUNCTION(GetCanvasMousePosition));

        // Begin〜 と End〜 で組にする部品
        RegisterEditorGUIContainers(r);

        // オブジェクトとアセットを選ぶ欄・画像
        RegisterEditorGUIAssets(r);
        RegisterEditorGUINodes(r);

        r.Namespace("");
        return r.Succeeded();
    }
}

#endif // CORE_EDITOR
