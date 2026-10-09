#include "pch.h"
#include "Editor/Script/EditorScriptBinding.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Inspector/InspectorLayout.h"
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

        /// @brief OnGUI 1 回分の状態
        struct GUIState
        {
            bool active = false;
            bool disabled = false;
            int indentLevel = 0;
            int pushedIds = 0;
            int canvasCount = 0;
            bool hasCanvas = false;
            bool canvasHovered = false;
            ImVec2 canvasMin{};
            ImVec2 canvasMax{};
        };

        GUIState g_gui;

        /// @brief OnGUI の中でなければスクリプトの例外にする
        bool RequireGUI(const char* function)
        {
            if (g_gui.active) {
                return true;
            }
            if (asIScriptContext* const context = asGetActiveContext()) {
                const std::string message = std::string("EditorGUI::") + function + " は EditorWindow の OnGUI の中でだけ使えます";
                context->SetException(message.c_str());
            }
            return false;
        }

        /// @brief Canvas の後でなければスクリプトの例外にする
        bool RequireCanvas(const char* function)
        {
            if (!RequireGUI(function)) {
                return false;
            }
            if (g_gui.hasCanvas) {
                return true;
            }
            if (asIScriptContext* const context = asGetActiveContext()) {
                const std::string message = std::string("EditorGUI::") + function + " は EditorGUI::Canvas の後で呼びます";
                context->SetException(message.c_str());
            }
            return false;
        }

        /// @brief 表示する部分（`##` より前）
        std::string DisplayPart(const std::string& label)
        {
            return label.substr(0, label.find("##"));
        }

        /// @brief 欄の ID（ラベルは左の列に描くので、欄そのものには表示しない）
        std::string FieldId(const std::string& label)
        {
            return "##" + label;
        }

        /// @brief 左の列にラベルを描き、次の欄を右の列へ置く
        void BeginRow(const std::string& label)
        {
            InspectorLayout::BeginRow(DisplayPart(label).c_str(), Theme::kTextDim);
        }

        /// @brief sRGB の色を ImGui の描画先（リニア）の色にする
        ImVec4 ToLinear(const Vector4& color)
        {
            const auto channel = [](float value) {
                const float c = std::clamp(value, 0.0f, 1.0f);
                return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                };
            return ImVec4(channel(color.x), channel(color.y), channel(color.z), std::clamp(color.w, 0.0f, 1.0f));
        }

        ImU32 ToColorU32(const Vector4& color)
        {
            return ImGui::ColorConvertFloat4ToU32(ToLinear(color));
        }

        /// @brief Canvas の左上を原点にした座標を画面の座標にする
        ImVec2 CanvasToScreen(const Vector2& position)
        {
            return ImVec2(g_gui.canvasMin.x + position.x, g_gui.canvasMin.y + position.y);
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

        // ---------------------------------------------------------------- 入力の部品

        bool Button(const std::string& label, float width)
        {
            if (!RequireGUI("Button")) {
                return false;
            }
            return ImGui::Button(label.c_str(), ImVec2(width < 0.0f ? -FLT_MIN : width, 0.0f));
        }

        bool Toggle(const std::string& label, bool value)
        {
            if (!RequireGUI("Toggle")) {
                return value;
            }
            BeginRow(label);
            ImGui::Checkbox(FieldId(label).c_str(), &value);
            return value;
        }

        int IntField(const std::string& label, int value)
        {
            if (!RequireGUI("IntField")) {
                return value;
            }
            BeginRow(label);
            ImGui::DragInt(FieldId(label).c_str(), &value, 0.1f);
            return value;
        }

        float FloatField(const std::string& label, float value)
        {
            if (!RequireGUI("FloatField")) {
                return value;
            }
            BeginRow(label);
            ImGui::DragFloat(FieldId(label).c_str(), &value, 0.01f, 0.0f, 0.0f, "%.3f");
            return value;
        }

        std::string TextField(const std::string& label, const std::string& value)
        {
            if (!RequireGUI("TextField")) {
                return value;
            }
            BeginRow(label);
            std::string text = value;
            ImGui::InputText(FieldId(label).c_str(), text.data(), text.capacity() + 1,
                ImGuiInputTextFlags_CallbackResize, &ResizeStringCallback, &text);
            return text;
        }

        int IntSlider(const std::string& label, int value, int min, int max)
        {
            if (!RequireGUI("IntSlider")) {
                return value;
            }
            BeginRow(label);
            ImGui::SliderInt(FieldId(label).c_str(), &value, min, max);
            return value;
        }

        float Slider(const std::string& label, float value, float min, float max)
        {
            if (!RequireGUI("Slider")) {
                return value;
            }
            BeginRow(label);
            ImGui::SliderFloat(FieldId(label).c_str(), &value, min, max, "%.3f");
            return value;
        }

        Vector2 Vector2Field(const std::string& label, const Vector2& value)
        {
            if (!RequireGUI("Vector2Field")) {
                return value;
            }
            BeginRow(label);
            float components[2] = { value.x, value.y };
            ImGui::DragFloat2(FieldId(label).c_str(), components, 0.05f, 0.0f, 0.0f, "%.3f");
            return Vector2{ components[0], components[1] };
        }

        Vector3 Vector3Field(const std::string& label, const Vector3& value)
        {
            if (!RequireGUI("Vector3Field")) {
                return value;
            }
            BeginRow(label);
            float components[3] = { value.x, value.y, value.z };
            ImGui::DragFloat3(FieldId(label).c_str(), components, 0.05f, 0.0f, 0.0f, "%.3f");
            return Vector3{ components[0], components[1], components[2] };
        }

        Vector4 ColorField(const std::string& label, const Vector4& value)
        {
            if (!RequireGUI("ColorField")) {
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
                ImGui::ColorPicker4("##picker", rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
                ImGui::EndPopup();
            }
            ImGui::PopID();
            return Vector4{ rgba[0], rgba[1], rgba[2], rgba[3] };
        }

        int Popup(const std::string& label, int selected, const CScriptArray& options)
        {
            if (!RequireGUI("Popup")) {
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
                    if (ImGui::Selectable(optionAt(i), i == selected)) {
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

        // ---------------------------------------------------------------- 状態

        bool GetEnabled()
        {
            return !g_gui.disabled;
        }

        void SetEnabled(bool enabled)
        {
            if (!RequireGUI("enabled")) {
                return;
            }
            if (!enabled && !g_gui.disabled) {
                ImGui::BeginDisabled(true);
                g_gui.disabled = true;
            } else if (enabled && g_gui.disabled) {
                ImGui::EndDisabled();
                g_gui.disabled = false;
            }
        }

        int GetIndentLevel()
        {
            return g_gui.indentLevel;
        }

        void SetIndentLevel(int level)
        {
            if (!RequireGUI("indentLevel")) {
                return;
            }
            level = (std::max)(0, level);
            for (; g_gui.indentLevel < level; ++g_gui.indentLevel) {
                ImGui::Indent();
            }
            for (; g_gui.indentLevel > level; --g_gui.indentLevel) {
                ImGui::Unindent();
            }
        }

        void PushIdInt(int id)
        {
            if (RequireGUI("PushID")) {
                ImGui::PushID(id);
                ++g_gui.pushedIds;
            }
        }

        void PushIdString(const std::string& id)
        {
            if (RequireGUI("PushID")) {
                ImGui::PushID(id.c_str());
                ++g_gui.pushedIds;
            }
        }

        void PopId()
        {
            if (!RequireGUI("PopID")) {
                return;
            }
            if (g_gui.pushedIds <= 0) {
                if (asIScriptContext* const context = asGetActiveContext()) {
                    context->SetException("EditorGUI::PopID が PushID より多く呼ばれました");
                }
                return;
            }
            ImGui::PopID();
            --g_gui.pushedIds;
        }

        // ---------------------------------------------------------------- 絵を描く場所

        Vector2 Canvas(float width, float height)
        {
            if (!RequireGUI("Canvas")) {
                return Vector2{};
            }
            const float w = (std::max)(1.0f, width > 0.0f ? width : ImGui::GetContentRegionAvail().x);
            const float h = (std::max)(1.0f, height > 0.0f ? height : w);

            ImGui::PushID(g_gui.canvasCount++);
            ImGui::InvisibleButton("##canvas", ImVec2(w, h),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            ImGui::PopID();

            g_gui.hasCanvas = true;
            g_gui.canvasMin = ImGui::GetItemRectMin();
            g_gui.canvasMax = ImGui::GetItemRectMax();
            g_gui.canvasHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(g_gui.canvasMin, g_gui.canvasMax, ImGui::GetColorU32(Theme::kDeepest));
            drawList->AddRect(g_gui.canvasMin, g_gui.canvasMax, ImGui::GetColorU32(Theme::kOutline));
            return Vector2{ w, h };
        }

        void DrawRect(const Vector2& min, const Vector2& max, const Vector4& color)
        {
            if (!RequireCanvas("DrawRect")) {
                return;
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(g_gui.canvasMin, g_gui.canvasMax, true);
            drawList->AddRectFilled(CanvasToScreen(min), CanvasToScreen(max), ToColorU32(color));
            drawList->PopClipRect();
        }

        void DrawRectOutline(const Vector2& min, const Vector2& max, const Vector4& color, float thickness)
        {
            if (!RequireCanvas("DrawRectOutline")) {
                return;
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(g_gui.canvasMin, g_gui.canvasMax, true);
            drawList->AddRect(CanvasToScreen(min), CanvasToScreen(max), ToColorU32(color), 0.0f, 0, thickness);
            drawList->PopClipRect();
        }

        void DrawLine(const Vector2& from, const Vector2& to, const Vector4& color, float thickness)
        {
            if (!RequireCanvas("DrawLine")) {
                return;
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(g_gui.canvasMin, g_gui.canvasMax, true);
            drawList->AddLine(CanvasToScreen(from), CanvasToScreen(to), ToColorU32(color), thickness);
            drawList->PopClipRect();
        }

        void DrawCanvasText(const Vector2& position, const std::string& text, const Vector4& color)
        {
            if (!RequireCanvas("DrawText")) {
                return;
            }
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(g_gui.canvasMin, g_gui.canvasMax, true);
            drawList->AddText(CanvasToScreen(position), ToColorU32(color), text.c_str());
            drawList->PopClipRect();
        }

        bool IsCanvasHovered()
        {
            return RequireCanvas("IsCanvasHovered") && g_gui.canvasHovered;
        }

        Vector2 GetCanvasMousePosition()
        {
            if (!RequireCanvas("GetCanvasMousePosition")) {
                return Vector2{};
            }
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            return Vector2{ mouse.x - g_gui.canvasMin.x, mouse.y - g_gui.canvasMin.y };
        }

        bool IsMouseDown(int button)
        {
            return RequireGUI("IsMouseDown") && button >= 0 && button < ImGuiMouseButton_COUNT
                && ImGui::IsMouseDown(button);
        }

        bool IsMouseClicked(int button)
        {
            return RequireGUI("IsMouseClicked") && button >= 0 && button < ImGuiMouseButton_COUNT
                && ImGui::IsMouseClicked(button);
        }
    }

    GUIScope::GUIScope()
    {
        g_gui = GUIState{};
        g_gui.active = true;
    }

    GUIScope::~GUIScope()
    {
        if (g_gui.disabled) {
            ImGui::EndDisabled();
        }
        for (; g_gui.indentLevel > 0; --g_gui.indentLevel) {
            ImGui::Unindent();
        }
        for (; g_gui.pushedIds > 0; --g_gui.pushedIds) {
            ImGui::PopID();
        }
        g_gui = GUIState{};
    }

    bool RegisterEditorGUI(asIScriptEngine* engine)
    {
        Script::BindingRegistrar r(engine);
        r.Namespace("EditorGUI");

        r.Enum("MessageType");
        r.EnumValue("MessageType", "Info", static_cast<int>(MessageType::Info));
        r.EnumValue("MessageType", "Warning", static_cast<int>(MessageType::Warning));
        r.EnumValue("MessageType", "Error", static_cast<int>(MessageType::Error));

        r.Function("void Label(const string &in text)", asFUNCTION(Label));
        r.Function("void Label(const string &in label, const string &in value)", asFUNCTION(LabelValue));
        r.Function("void Header(const string &in text)", asFUNCTION(Header));
        r.Function("void HelpBox(const string &in message, MessageType type = EditorGUI::MessageType::Info)",
            asFUNCTION(HelpBox));
        r.Function("void Separator()", asFUNCTION(Separator));
        r.Function("void Space(float height = 6)", asFUNCTION(Space));
        r.Function("void SameLine()", asFUNCTION(SameLine));
        r.Function("float GetAvailableWidth()", asFUNCTION(GetAvailableWidth));

        r.Function("bool Button(const string &in label, float width = 0)", asFUNCTION(Button));
        r.Function("bool Toggle(const string &in label, bool value)", asFUNCTION(Toggle));
        r.Function("int IntField(const string &in label, int value)", asFUNCTION(IntField));
        r.Function("float FloatField(const string &in label, float value)", asFUNCTION(FloatField));
        r.Function("string TextField(const string &in label, const string &in value)", asFUNCTION(TextField));
        r.Function("int IntSlider(const string &in label, int value, int min, int max)", asFUNCTION(IntSlider));
        r.Function("float Slider(const string &in label, float value, float min, float max)", asFUNCTION(Slider));
        r.Function("Vector2 Vector2Field(const string &in label, const Vector2 &in value)", asFUNCTION(Vector2Field));
        r.Function("Vector3 Vector3Field(const string &in label, const Vector3 &in value)", asFUNCTION(Vector3Field));
        r.Function("Vector4 ColorField(const string &in label, const Vector4 &in value)", asFUNCTION(ColorField));
        r.Function("int Popup(const string &in label, int selected, const array<string> &in options)", asFUNCTION(Popup));
        r.Function("bool Foldout(const string &in label, bool defaultOpen = true)", asFUNCTION(Foldout));

        r.Function("bool get_enabled() property", asFUNCTION(GetEnabled));
        r.Function("void set_enabled(bool enabled) property", asFUNCTION(SetEnabled));
        r.Function("int get_indentLevel() property", asFUNCTION(GetIndentLevel));
        r.Function("void set_indentLevel(int level) property", asFUNCTION(SetIndentLevel));
        r.Function("void PushID(int id)", asFUNCTION(PushIdInt));
        r.Function("void PushID(const string &in id)", asFUNCTION(PushIdString));
        r.Function("void PopID()", asFUNCTION(PopId));

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
        r.Function("bool IsMouseDown(int button = 0)", asFUNCTION(IsMouseDown));
        r.Function("bool IsMouseClicked(int button = 0)", asFUNCTION(IsMouseClicked));

        r.Namespace("");
        return r.Succeeded();
    }
}

#endif // CORE_EDITOR
