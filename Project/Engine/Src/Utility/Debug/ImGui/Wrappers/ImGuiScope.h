#pragma once

#ifdef USE_IMGUI

#include <imgui.h>

namespace CoreEngine {
    namespace UI {
        namespace Scope {

            /// @brief TreeNode/TreePop を RAII で管理する
            /// @note if (auto s = TreeScope("label")) { ... } の形で使う
            /// @note flags に ImGuiTreeNodeFlags_DefaultOpen などを渡すことで
            ///       TreeNodeEx と同等の挙動になる
            struct TreeScope {
                const bool open;
                explicit TreeScope(const char* label, ImGuiTreeNodeFlags flags = 0)
                    : open(ImGui::TreeNodeEx(label, flags)) {
                }
                ~TreeScope() { if (open) ImGui::TreePop(); }
                explicit operator bool() const { return open; }
            };

            /// @brief CollapsingHeader を RAII で管理する
            struct CollapsingScope {
                const bool open;
                explicit CollapsingScope(const char* label, ImGuiTreeNodeFlags flags = 0)
                    : open(ImGui::CollapsingHeader(label, flags)) {
                }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginDisabled/EndDisabled を RAII で管理する
            struct DisabledScope {
                explicit DisabledScope(bool disabled = true) { ImGui::BeginDisabled(disabled); }
                ~DisabledScope() { ImGui::EndDisabled(); }
            };

            /// @brief Indent/Unindent を RAII で管理する
            struct IndentScope {
                explicit IndentScope(float indent = 0.0f) { ImGui::Indent(indent); }
                ~IndentScope() { ImGui::Unindent(); }
            };

            /// @brief Begin/End ウィンドウを RAII で管理する
            /// @note open が false でもデストラクタで End() を呼ぶ必要があるため常に End() を呼ぶ
            struct WindowScope {
                const bool open;
                explicit WindowScope(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0)
                    : open(ImGui::Begin(name, p_open, flags)) {
                }
                ~WindowScope() { ImGui::End(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginTabBar/EndTabBar を RAII で管理する
            struct TabBarScope {
                const bool open;
                explicit TabBarScope(const char* str_id, ImGuiTabBarFlags flags = 0)
                    : open(ImGui::BeginTabBar(str_id, flags)) {
                }
                ~TabBarScope() { if (open) ImGui::EndTabBar(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginTabItem/EndTabItem を RAII で管理する
            struct TabItemScope {
                const bool open;
                explicit TabItemScope(const char* label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0)
                    : open(ImGui::BeginTabItem(label, p_open, flags)) {
                }
                ~TabItemScope() { if (open) ImGui::EndTabItem(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginPopup/EndPopup を RAII で管理する
            struct PopupScope {
                const bool open;
                explicit PopupScope(const char* str_id, ImGuiWindowFlags flags = 0)
                    : open(ImGui::BeginPopup(str_id, flags)) {
                }
                ~PopupScope() { if (open) ImGui::EndPopup(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginChild/EndChild を RAII で管理する
            /// @note EndChild は常に呼び出す必要があるためデストラクタで無条件に呼ぶ
            /// @note if (auto child = ChildScope("id", {0, 200}, ImGuiChildFlags_Border)) { ... }
            struct ChildScope {
                const bool open;
                explicit ChildScope(const char* str_id,
                    ImVec2 size = ImVec2(0, 0),
                    ImGuiChildFlags child_flags = 0,
                    ImGuiWindowFlags window_flags = 0)
                    : open(ImGui::BeginChild(str_id, size, child_flags, window_flags)) {
                }
                ~ChildScope() { ImGui::EndChild(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginListBox/EndListBox を RAII で管理する
            /// @note if (auto lb = ListBoxScope("label", {-1, 180})) { ... }
            struct ListBoxScope {
                const bool open;
                explicit ListBoxScope(const char* label, ImVec2 size = ImVec2(0, 0))
                    : open(ImGui::BeginListBox(label, size)) {
                }
                ~ListBoxScope() { if (open) ImGui::EndListBox(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginPopupModal/EndPopup を RAII で管理する
            /// @note モーダルダイアログ。OpenPopup(name) で事前に開く必要がある
            /// @note if (auto m = ModalScope("確認")) { ... }
            struct ModalScope {
                const bool open;
                explicit ModalScope(const char* name,
                    bool* p_open = nullptr,
                    ImGuiWindowFlags flags = 0)
                    : open(ImGui::BeginPopupModal(name, p_open, flags)) {
                }
                ~ModalScope() { if (open) ImGui::EndPopup(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginGroup/EndGroup を RAII で管理する
            /// @note グループ内のウィジェットを一つの項目として扱う
            /// @note if (auto g = GroupScope()) { ... }  または  { UI::Scope::GroupScope g; ... }
            struct GroupScope {
                GroupScope() { ImGui::BeginGroup(); }
                ~GroupScope() { ImGui::EndGroup(); }
            };

            /// @brief BeginTable/EndTable を RAII で管理する
            /// @note if (auto t = TableScope("id", 3)) { ImGui::TableSetupColumn(...); ... }
            struct TableScope {
                const bool open;
                explicit TableScope(const char* str_id,
                    int column,
                    ImGuiTableFlags flags  = 0,
                    ImVec2 outer_size      = ImVec2(0.0f, 0.0f),
                    float  inner_width     = 0.0f)
                    : open(ImGui::BeginTable(str_id, column, flags, outer_size, inner_width)) {
                }
                ~TableScope() { if (open) ImGui::EndTable(); }
                explicit operator bool() const { return open; }
            };

            /// @brief BeginTooltip/EndTooltip を RAII で管理する
            /// @note if (auto tt = TooltipScope()) { ... }
            struct TooltipScope {
                const bool open;
                explicit TooltipScope() : open(ImGui::BeginTooltip()) {}
                ~TooltipScope() { if (open) ImGui::EndTooltip(); }
                explicit operator bool() const { return open; }
            };

        } // namespace Scope
    } // namespace UI
} // namespace CoreEngine

#endif // USE_IMGUI
