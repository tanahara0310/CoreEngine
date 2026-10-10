#include "pch.h"
#include "Editor/Script/EditorGUIState.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "Editor/Script/EditorScriptBinding.h"

#include <angelscript.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        GUIFrameState g_state;
        std::vector<GUIScopeEntry> g_scopes;

        /// @brief 中身を別のウィンドウに描く範囲か（字下げをウィンドウごとに数える）
        bool IsWindowScope(GUIScopeKind kind)
        {
            switch (kind) {
            case GUIScopeKind::Child:
            case GUIScopeKind::Table:
            case GUIScopeKind::ListBox:
            case GUIScopeKind::Popup:
            case GUIScopeKind::Menu:
            case GUIScopeKind::NodeEditor:
            case GUIScopeKind::Node:
                return true;
            default:
                return false;
            }
        }

        /// @brief 範囲を開く関数の名前（例外の文に入れる）
        const char* BeginName(GUIScopeKind kind)
        {
            switch (kind) {
            case GUIScopeKind::Id: return "PushID";
            case GUIScopeKind::Group: return "BeginGroup";
            case GUIScopeKind::Child: return "BeginChild";
            case GUIScopeKind::TreeNode: return "BeginTreeNode";
            case GUIScopeKind::TabBar: return "BeginTabBar";
            case GUIScopeKind::TabItem: return "BeginTabItem";
            case GUIScopeKind::Table: return "BeginTable";
            case GUIScopeKind::ListBox: return "BeginListBox";
            case GUIScopeKind::Popup: return "BeginPopup〜";
            case GUIScopeKind::Menu: return "BeginMenu";
            case GUIScopeKind::NodeEditor: return "BeginNodeEditor";
            case GUIScopeKind::Node: return "BeginNode";
            case GUIScopeKind::NodeTitle: return "BeginNodeTitle";
            case GUIScopeKind::InputPin: return "BeginInputPin";
            case GUIScopeKind::OutputPin: return "BeginOutputPin";
            }
            return "Begin〜";
        }

        /// @brief 範囲を閉じる（字下げを開いたときへ戻し、ImGui の End を呼ぶ）
        void CloseScope(const GUIScopeEntry& scope)
        {
            if (scope.kind == GUIScopeKind::Id) {
                ImGui::PopID();
                return;
            }

            const bool windowScope = IsWindowScope(scope.kind);
            SetIndentLevel(windowScope ? 0 : scope.savedIndent);

            switch (scope.kind) {
            case GUIScopeKind::Group: ImGui::EndGroup(); break;
            case GUIScopeKind::Child: ImGui::EndChild(); break;
            case GUIScopeKind::TreeNode: if (scope.open) { ImGui::TreePop(); } break;
            case GUIScopeKind::TabBar: if (scope.open) { ImGui::EndTabBar(); } break;
            case GUIScopeKind::TabItem: if (scope.open) { ImGui::EndTabItem(); } break;
            case GUIScopeKind::Table: if (scope.open) { ImGui::EndTable(); } break;
            case GUIScopeKind::ListBox: if (scope.open) { ImGui::EndListBox(); } break;
            case GUIScopeKind::Popup: if (scope.open) { ImGui::EndPopup(); } break;
            case GUIScopeKind::Menu: if (scope.open) { ImGui::EndMenu(); } break;
            case GUIScopeKind::NodeEditor:
            case GUIScopeKind::Node:
            case GUIScopeKind::NodeTitle:
            case GUIScopeKind::InputPin:
            case GUIScopeKind::OutputPin:
                CloseNodeScope(scope.kind);
                break;
            default: break;
            }

            if (windowScope) {
                g_state.indentLevel = scope.savedIndent;
            }
        }
    }

    GUIFrameState& FrameState()
    {
        return g_state;
    }

    void ThrowScriptException(const std::string& message)
    {
        if (asIScriptContext* const context = asGetActiveContext()) {
            context->SetException(message.c_str());
        }
    }

    bool RequireGUI(const char* function)
    {
        if (g_state.active) {
            return true;
        }
        ThrowScriptException(std::string("EditorGUI::") + function + " は EditorWindow の OnGUI の中でだけ使えます");
        return false;
    }

    WidgetScope::WidgetScope(const char* function)
        : ok_(RequireGUI(function)), disabled_(ok_ && !g_state.enabled)
    {
        if (disabled_) {
            ImGui::BeginDisabled(true);
        }
    }

    WidgetScope::~WidgetScope()
    {
        if (disabled_) {
            ImGui::EndDisabled();
        }
    }

    void SetIndentLevel(int level)
    {
        level = (std::max)(0, level);
        for (; g_state.indentLevel < level; ++g_state.indentLevel) {
            ImGui::Indent();
        }
        for (; g_state.indentLevel > level; --g_state.indentLevel) {
            ImGui::Unindent();
        }
    }

    std::string LabelDisplayPart(const std::string& label)
    {
        return label.substr(0, label.find("##"));
    }

    std::string LabelFieldId(const std::string& label)
    {
        return "##" + label;
    }

    void BeginLabeledRow(const std::string& label)
    {
        const std::string display = LabelDisplayPart(label);
        if (display.empty()) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            return;
        }
        InspectorLayout::BeginRow(display.c_str(), Theme::kTextDim);
    }

    ImVec4 SrgbToLinear(float r, float g, float b, float a)
    {
        const auto channel = [](float value) {
            const float c = std::clamp(value, 0.0f, 1.0f);
            return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
        return ImVec4(channel(r), channel(g), channel(b), std::clamp(a, 0.0f, 1.0f));
    }

    void PushGUIScope(GUIScopeKind kind, bool open)
    {
        g_scopes.push_back(GUIScopeEntry{ kind, open, g_state.indentLevel });
        if (IsWindowScope(kind)) {
            g_state.indentLevel = 0;
        }
    }

    bool PopGUIScope(GUIScopeKind kind, const char* function)
    {
        if (!RequireGUI(function)) {
            return false;
        }

        if (kind == GUIScopeKind::Id) {
            if (g_scopes.empty()) {
                ThrowScriptException("EditorGUI::PopID が PushID より多く呼ばれました");
                return false;
            }
            if (g_scopes.back().kind != GUIScopeKind::Id) {
                ThrowScriptException(std::string("EditorGUI::PopID の前に、") + BeginName(g_scopes.back().kind) + " を閉じてください");
                return false;
            }
            g_scopes.pop_back();
            ImGui::PopID();
            return true;
        }

        // 範囲の中で PushID したまま閉じるものは、先に戻す
        while (!g_scopes.empty() && g_scopes.back().kind == GUIScopeKind::Id) {
            ImGui::PopID();
            g_scopes.pop_back();
        }
        if (g_scopes.empty()) {
            ThrowScriptException(std::string("EditorGUI::") + function + " に対応する " + BeginName(kind) + " がありません");
            return false;
        }
        if (g_scopes.back().kind != kind) {
            ThrowScriptException(std::string("EditorGUI::") + function + " の前に、" + BeginName(g_scopes.back().kind)
                + " を閉じてください（Begin〜 は戻り値に関係なく End〜 と組にします）");
            return false;
        }

        const GUIScopeEntry scope = g_scopes.back();
        g_scopes.pop_back();
        CloseScope(scope);
        return true;
    }

    const GUIScopeEntry* InnermostWindowOrTable()
    {
        for (auto it = g_scopes.rbegin(); it != g_scopes.rend(); ++it) {
            if (IsWindowScope(it->kind)) {
                return &*it;
            }
        }
        return nullptr;
    }

    const GUIScopeEntry* InnermostScope()
    {
        for (auto it = g_scopes.rbegin(); it != g_scopes.rend(); ++it) {
            if (it->kind != GUIScopeKind::Id) {
                return &*it;
            }
        }
        return nullptr;
    }

    bool RejectInsideNodeEditor(const char* function)
    {
        const GUIScopeEntry* const scope = InnermostWindowOrTable();
        if (scope && (scope->kind == GUIScopeKind::NodeEditor || scope->kind == GUIScopeKind::Node)) {
            ThrowScriptException(std::string("EditorGUI::") + function + " はノードエディタとノードの中には置けません");
            return false;
        }
        return true;
    }

    void UnwindGUIScopes()
    {
        while (!g_scopes.empty()) {
            const GUIScopeEntry scope = g_scopes.back();
            g_scopes.pop_back();
            CloseScope(scope);
        }
        SetIndentLevel(0);
    }

    GUIScope::GUIScope(const std::string& windowKey)
    {
        g_scopes.clear();
        g_state = GUIFrameState{};
        g_state.active = true;
        g_state.windowKey = windowKey;
    }

    GUIScope::~GUIScope()
    {
        UnwindGUIScopes();
        g_state = GUIFrameState{};
    }
}

#endif // CORE_EDITOR
