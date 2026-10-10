#include "pch.h"
#include "Editor/Script/EditorGUIState.h"

#ifdef CORE_EDITOR

#include "Script/Binding/BindingRegistrar.h"

#include <angelscript.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <string>

namespace CoreEngine::Editor::ScriptBinding
{
    namespace
    {
        /// 表の列の数の上限
        constexpr int kMaxTableColumns = 64;

        /// @brief 表に付けられる設定（組み合わせは | でつなぐ）
        enum TableFlags : int
        {
            TableNone = 0,
            TableBorders = 1 << 0,
            TableRowBackground = 1 << 1,
            TableResizable = 1 << 2,
            TableSortable = 1 << 3,
            TableScrollY = 1 << 4,
            TableDefault = TableBorders | TableRowBackground | TableResizable,
        };

        /// @brief 一番内側の表が開いているか（表の外ならスクリプトの例外にする）
        bool RequireOpenTable(const char* function)
        {
            if (!RequireGUI(function)) {
                return false;
            }
            const GUIScopeEntry* const scope = InnermostWindowOrTable();
            if (!scope || scope->kind != GUIScopeKind::Table) {
                ThrowScriptException(std::string("EditorGUI::") + function + " は BeginTable と EndTable の間で呼びます");
                return false;
            }
            return scope->open;
        }

        // ---------------------------------------------------------------- まとまりと子の枠

        void BeginGroup()
        {
            if (RequireGUI("BeginGroup")) {
                ImGui::BeginGroup();
                PushGUIScope(GUIScopeKind::Group, true);
            }
        }

        void EndGroup()
        {
            PopGUIScope(GUIScopeKind::Group, "EndGroup");
        }

        bool BeginChild(const std::string& id, float width, float height, bool border)
        {
            if (!RequireGUI("BeginChild") || !RejectInsideNodeEditor("BeginChild")) {
                return false;
            }
            const bool visible = ImGui::BeginChild(id.c_str(), ImVec2(width, height),
                border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None);
            PushGUIScope(GUIScopeKind::Child, true);
            return visible;
        }

        void EndChild()
        {
            PopGUIScope(GUIScopeKind::Child, "EndChild");
        }

        // ---------------------------------------------------------------- 木

        bool BeginTreeNode(const std::string& label, bool defaultOpen, bool selected, bool leaf)
        {
            if (!RequireGUI("BeginTreeNode")) {
                return false;
            }
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
                | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (defaultOpen) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (selected) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (leaf) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            PushGUIScope(GUIScopeKind::TreeNode, open);
            return open;
        }

        void EndTreeNode()
        {
            PopGUIScope(GUIScopeKind::TreeNode, "EndTreeNode");
        }

        // ---------------------------------------------------------------- タブ

        bool BeginTabBar(const std::string& id)
        {
            if (!RequireGUI("BeginTabBar")) {
                return false;
            }
            const bool open = ImGui::BeginTabBar(id.c_str());
            PushGUIScope(GUIScopeKind::TabBar, open);
            return open;
        }

        void EndTabBar()
        {
            PopGUIScope(GUIScopeKind::TabBar, "EndTabBar");
        }

        bool BeginTabItem(const std::string& label)
        {
            if (!RequireGUI("BeginTabItem")) {
                return false;
            }
            const bool open = ImGui::BeginTabItem(label.c_str());
            PushGUIScope(GUIScopeKind::TabItem, open);
            return open;
        }

        void EndTabItem()
        {
            PopGUIScope(GUIScopeKind::TabItem, "EndTabItem");
        }

        // ---------------------------------------------------------------- 表

        bool BeginTable(const std::string& id, int columns, int flags, float height)
        {
            if (!RequireGUI("BeginTable") || !RejectInsideNodeEditor("BeginTable")) {
                return false;
            }
            if (columns < 1 || columns > kMaxTableColumns) {
                ThrowScriptException("EditorGUI::BeginTable の列の数は 1〜" + std::to_string(kMaxTableColumns) + " です");
                return false;
            }
            ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp;
            if ((flags & TableBorders) != 0) {
                tableFlags |= ImGuiTableFlags_Borders;
            }
            if ((flags & TableRowBackground) != 0) {
                tableFlags |= ImGuiTableFlags_RowBg;
            }
            if ((flags & TableResizable) != 0) {
                tableFlags |= ImGuiTableFlags_Resizable;
            }
            if ((flags & TableSortable) != 0) {
                tableFlags |= ImGuiTableFlags_Sortable;
            }
            if ((flags & TableScrollY) != 0) {
                tableFlags |= ImGuiTableFlags_ScrollY;
            }
            const bool open = ImGui::BeginTable(id.c_str(), columns, tableFlags, ImVec2(0.0f, (std::max)(0.0f, height)));
            PushGUIScope(GUIScopeKind::Table, open);
            return open;
        }

        void EndTable()
        {
            PopGUIScope(GUIScopeKind::Table, "EndTable");
        }

        void TableSetupColumn(const std::string& label, float width)
        {
            if (!RequireOpenTable("TableSetupColumn")) {
                return;
            }
            const ImGuiTableColumnFlags flags = width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;
            ImGui::TableSetupColumn(label.c_str(), flags, (std::max)(0.0f, width));
        }

        void TableSetupScrollFreeze(int columns, int rows)
        {
            if (RequireOpenTable("TableSetupScrollFreeze")) {
                ImGui::TableSetupScrollFreeze((std::max)(0, columns), (std::max)(0, rows));
            }
        }

        void TableHeadersRow()
        {
            if (RequireOpenTable("TableHeadersRow")) {
                ImGui::TableHeadersRow();
            }
        }

        void TableNextRow()
        {
            if (RequireOpenTable("TableNextRow")) {
                ImGui::TableNextRow();
            }
        }

        bool TableNextColumn()
        {
            return RequireOpenTable("TableNextColumn") && ImGui::TableNextColumn();
        }

        bool TableSetColumnIndex(int column)
        {
            if (!RequireOpenTable("TableSetColumnIndex")) {
                return false;
            }
            if (column < 0 || column >= ImGui::TableGetColumnCount()) {
                ThrowScriptException("EditorGUI::TableSetColumnIndex の列の番号が表の列の数を超えています");
                return false;
            }
            return ImGui::TableSetColumnIndex(column);
        }

        bool IsTableSortChanged()
        {
            if (!RequireOpenTable("IsTableSortChanged")) {
                return false;
            }
            ImGuiTableSortSpecs* const specs = ImGui::TableGetSortSpecs();
            if (!specs || !specs->SpecsDirty) {
                return false;
            }
            specs->SpecsDirty = false;
            return true;
        }

        int GetTableSortColumn()
        {
            if (!RequireOpenTable("GetTableSortColumn")) {
                return -1;
            }
            const ImGuiTableSortSpecs* const specs = ImGui::TableGetSortSpecs();
            return (specs && specs->SpecsCount > 0) ? specs->Specs[0].ColumnIndex : -1;
        }

        bool IsTableSortAscending()
        {
            if (!RequireOpenTable("IsTableSortAscending")) {
                return true;
            }
            const ImGuiTableSortSpecs* const specs = ImGui::TableGetSortSpecs();
            return !specs || specs->SpecsCount == 0 || specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
        }

        // ---------------------------------------------------------------- 一覧

        bool BeginListBox(const std::string& label, float height)
        {
            if (!RequireGUI("BeginListBox") || !RejectInsideNodeEditor("BeginListBox")) {
                return false;
            }
            BeginLabeledRow(label);
            const bool open = ImGui::BeginListBox(LabelFieldId(label).c_str(), ImVec2(-FLT_MIN, (std::max)(0.0f, height)));
            PushGUIScope(GUIScopeKind::ListBox, open);
            return open;
        }

        void EndListBox()
        {
            PopGUIScope(GUIScopeKind::ListBox, "EndListBox");
        }

        // ---------------------------------------------------------------- ポップアップとメニュー

        void OpenPopup(const std::string& id)
        {
            if (RequireGUI("OpenPopup")) {
                ImGui::OpenPopup(id.c_str());
            }
        }

        bool BeginPopup(const std::string& id)
        {
            if (!RequireGUI("BeginPopup")) {
                return false;
            }
            const bool open = ImGui::BeginPopup(id.c_str());
            PushGUIScope(GUIScopeKind::Popup, open);
            return open;
        }

        bool BeginPopupModal(const std::string& title)
        {
            if (!RequireGUI("BeginPopupModal")) {
                return false;
            }
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            const bool open = ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            PushGUIScope(GUIScopeKind::Popup, open);
            return open;
        }

        bool BeginPopupContextItem(const std::string& id)
        {
            if (!RequireGUI("BeginPopupContextItem")) {
                return false;
            }
            if (id.empty() && ImGui::GetItemID() == 0) {
                ThrowScriptException("直前の部品に ID が無いので、EditorGUI::BeginPopupContextItem に ID を渡してください");
                return false;
            }
            const bool open = ImGui::BeginPopupContextItem(id.empty() ? nullptr : id.c_str());
            PushGUIScope(GUIScopeKind::Popup, open);
            return open;
        }

        bool BeginPopupContextWindow(const std::string& id)
        {
            if (!RequireGUI("BeginPopupContextWindow")) {
                return false;
            }
            const bool open = ImGui::BeginPopupContextWindow(id.empty() ? nullptr : id.c_str(),
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems);
            PushGUIScope(GUIScopeKind::Popup, open);
            return open;
        }

        void EndPopup()
        {
            PopGUIScope(GUIScopeKind::Popup, "EndPopup");
        }

        void CloseCurrentPopup()
        {
            if (!RequireGUI("CloseCurrentPopup")) {
                return;
            }
            const GUIScopeEntry* const scope = InnermostWindowOrTable();
            if (!scope || (scope->kind != GUIScopeKind::Popup && scope->kind != GUIScopeKind::Menu) || !scope->open) {
                ThrowScriptException("EditorGUI::CloseCurrentPopup は、開いたポップアップの中で呼びます");
                return;
            }
            ImGui::CloseCurrentPopup();
        }

        bool BeginMenu(const std::string& label, bool enabled)
        {
            if (!RequireGUI("BeginMenu")) {
                return false;
            }
            const bool open = ImGui::BeginMenu(label.c_str(), enabled && FrameState().enabled);
            PushGUIScope(GUIScopeKind::Menu, open);
            return open;
        }

        void EndMenu()
        {
            PopGUIScope(GUIScopeKind::Menu, "EndMenu");
        }

        bool MenuItem(const std::string& label, const std::string& shortcut, bool checked, bool enabled)
        {
            WidgetScope widget("MenuItem");
            return widget && ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked, enabled);
        }
    }

    void RegisterEditorGUIContainers(Script::BindingRegistrar& r)
    {
        r.Enum("TableFlags");
        r.EnumValue("TableFlags", "None", TableNone);
        r.EnumValue("TableFlags", "Borders", TableBorders);
        r.EnumValue("TableFlags", "RowBackground", TableRowBackground);
        r.EnumValue("TableFlags", "Resizable", TableResizable);
        r.EnumValue("TableFlags", "Sortable", TableSortable);
        r.EnumValue("TableFlags", "ScrollY", TableScrollY);
        r.EnumValue("TableFlags", "Default", TableDefault);

        r.Function("void BeginGroup()", asFUNCTION(BeginGroup));
        r.Function("void EndGroup()", asFUNCTION(EndGroup));
        r.Function("bool BeginChild(const string &in id, float width = 0, float height = 0, bool border = true)",
            asFUNCTION(BeginChild));
        r.Function("void EndChild()", asFUNCTION(EndChild));

        r.Function("bool BeginTreeNode(const string &in label, bool defaultOpen = false, bool selected = false, bool leaf = false)",
            asFUNCTION(BeginTreeNode));
        r.Function("void EndTreeNode()", asFUNCTION(EndTreeNode));

        r.Function("bool BeginTabBar(const string &in id)", asFUNCTION(BeginTabBar));
        r.Function("void EndTabBar()", asFUNCTION(EndTabBar));
        r.Function("bool BeginTabItem(const string &in label)", asFUNCTION(BeginTabItem));
        r.Function("void EndTabItem()", asFUNCTION(EndTabItem));

        r.Function("bool BeginTable(const string &in id, int columns, int flags = EditorGUI::TableFlags::Default, float height = 0)",
            asFUNCTION(BeginTable));
        r.Function("void EndTable()", asFUNCTION(EndTable));
        r.Function("void TableSetupColumn(const string &in label, float width = 0)", asFUNCTION(TableSetupColumn));
        r.Function("void TableSetupScrollFreeze(int columns, int rows)", asFUNCTION(TableSetupScrollFreeze));
        r.Function("void TableHeadersRow()", asFUNCTION(TableHeadersRow));
        r.Function("void TableNextRow()", asFUNCTION(TableNextRow));
        r.Function("bool TableNextColumn()", asFUNCTION(TableNextColumn));
        r.Function("bool TableSetColumnIndex(int column)", asFUNCTION(TableSetColumnIndex));
        r.Function("bool IsTableSortChanged()", asFUNCTION(IsTableSortChanged));
        r.Function("int GetTableSortColumn()", asFUNCTION(GetTableSortColumn));
        r.Function("bool IsTableSortAscending()", asFUNCTION(IsTableSortAscending));

        r.Function("bool BeginListBox(const string &in label, float height = 0)", asFUNCTION(BeginListBox));
        r.Function("void EndListBox()", asFUNCTION(EndListBox));

        r.Function("void OpenPopup(const string &in id)", asFUNCTION(OpenPopup));
        r.Function("bool BeginPopup(const string &in id)", asFUNCTION(BeginPopup));
        r.Function("bool BeginPopupModal(const string &in title)", asFUNCTION(BeginPopupModal));
        r.Function("bool BeginPopupContextItem(const string &in id = \"\")", asFUNCTION(BeginPopupContextItem));
        r.Function("bool BeginPopupContextWindow(const string &in id = \"\")", asFUNCTION(BeginPopupContextWindow));
        r.Function("void EndPopup()", asFUNCTION(EndPopup));
        r.Function("void CloseCurrentPopup()", asFUNCTION(CloseCurrentPopup));
        r.Function("bool BeginMenu(const string &in label, bool enabled = true)", asFUNCTION(BeginMenu));
        r.Function("void EndMenu()", asFUNCTION(EndMenu));
        r.Function("bool MenuItem(const string &in label, const string &in shortcut = \"\", bool checked = false, bool enabled = true)",
            asFUNCTION(MenuItem));
    }
}

#endif // CORE_EDITOR
