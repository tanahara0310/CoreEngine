#include "pch.h"
#include "Editor/ImGui/ProjectSettingsWindow.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <string_view>

namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 保存先（CVarSettingsSection と同じ振り分け）
        constexpr const char* kSharedPath = "Config/EngineSettings/CVars.json";
        constexpr const char* kPersonalPath = "Saved/EditorSettings/EditorState.json";

        /// 左のツリーの幅
        constexpr float kTreeWidth = 250.0f;

        /// @brief 大文字小文字を無視した部分一致
        bool ContainsIgnoreCase(std::string_view text, std::string_view query)
        {
            if (query.empty()) {
                return true;
            }
            const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
            const auto found = std::search(text.begin(), text.end(), query.begin(), query.end(),
                [&lower](char a, char b) { return lower(a) == lower(b); });
            return found != text.end();
        }

        /// @brief UI に出す CVar か
        bool IsListed(const ICVar* cvar)
        {
            return cvar && !HasFlag(cvar->GetFlags(), CVarFlags::NoUI);
        }

        /// @brief 接頭辞に一致し、UI に出す CVar
        std::vector<ICVar*> ListedByPrefix(std::string_view prefix)
        {
            std::vector<ICVar*> items = CVarRegistry::Get().GetByPrefix(prefix);
            std::erase_if(items, [](const ICVar* cvar) { return !IsListed(cvar); });
            return items;
        }
    }

    void ProjectSettingsWindow::RebuildFamilies()
    {
        const std::vector<ICVar*>& all = CVarRegistry::Get().GetAll();
        seenCVarCount_ = all.size();

        // 接頭辞 → まとまり（名前順）
        std::map<std::string, Group> groups;
        for (const ICVar* const cvar : all) {
            if (!IsListed(cvar)) {
                continue;
            }
            const std::string_view name = cvar->GetName();
            const size_t firstDot = name.find('.');
            if (firstDot == std::string_view::npos) {
                Group& group = groups[std::string(name)];
                group.prefix = name;
                group.label = name;
                ++group.count;
                continue;
            }

            // 3 段以上は「1 段目.2 段目.」でまとめ、2 段の名前はそれ自体を 1 つのまとまりにする
            const size_t secondDot = name.find('.', firstDot + 1);
            const std::string prefix = secondDot == std::string_view::npos
                ? std::string(name)
                : std::string(name.substr(0, secondDot + 1));
            const std::string_view second = secondDot == std::string_view::npos
                ? name.substr(firstDot + 1)
                : name.substr(firstDot + 1, secondDot - firstDot - 1);

            Group& group = groups[prefix];
            group.prefix = prefix;
            group.label = second;
            ++group.count;
        }

        Family rendering{ "描画（r.）", false, {} };
        Family system{ "システム（sys.）", false, {} };
        Family editor{ "エディタ（d.）", true, {} };
        std::map<std::string, Family> others;
        for (auto& [prefix, group] : groups) {
            const std::string family = prefix.substr(0, prefix.find('.'));
            if (family == "r") {
                rendering.groups.push_back(group);
            } else if (family == "sys") {
                system.groups.push_back(group);
            } else if (family == "d") {
                editor.groups.push_back(group);
            } else {
                Family& other = others[family];
                other.label = family + ".";
                other.groups.push_back(group);
            }
        }

        families_.clear();
        for (Family* const family : { &rendering, &system }) {
            if (!family->groups.empty()) {
                families_.push_back(std::move(*family));
            }
        }
        for (auto& [name, family] : others) {
            families_.push_back(std::move(family));
        }
        if (!editor.groups.empty()) {
            families_.push_back(std::move(editor));
        }
    }

    const ProjectSettingsWindow::Group* ProjectSettingsWindow::FindSelectedGroup(bool* personal) const
    {
        for (const Family& family : families_) {
            for (const Group& group : family.groups) {
                if (group.prefix == selectedPrefix_) {
                    if (personal) {
                        *personal = family.personal;
                    }
                    return &group;
                }
            }
        }
        return nullptr;
    }

    void ProjectSettingsWindow::Draw(bool& visible)
    {
        if (!visible) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(880.0f, 600.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 300.0f), ImVec2(2400.0f, 1600.0f));
        auto window = UI::Scope::WindowScope("Project Settings", &visible);
        if (!window) {
            return;
        }

        if (CVarRegistry::Get().GetAll().size() != seenCVarCount_) {
            RebuildFamilies();
        }

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        if (auto tree = UI::Scope::ChildScope("##settingsTree", ImVec2(kTreeWidth, -footerHeight), ImGuiChildFlags_Borders)) {
            DrawTree();
        }

        ImGui::SameLine();
        if (auto pane = UI::Scope::ChildScope("##settingsPane", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_Borders)) {
            if (filter_[0] != '\0') {
                DrawSearchResults();
            } else if (!selectedSection_.empty()) {
                DrawSectionPane();
            } else {
                DrawGroupPane();
            }
        }

        DrawFooter();
    }

    void ProjectSettingsWindow::DrawTree()
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##settingsFilter", "名前で検索…", filter_, sizeof(filter_));
        ImGui::Spacing();

        // 行を選ぶと検索欄を空にするので、このフレームの検索語は写しておく
        const std::string query = filter_;

        // まとまりに検索語を含む項目があるか
        const auto groupMatches = [&query](const Group& group) {
            if (query.empty() || ContainsIgnoreCase(group.label, query)) {
                return true;
            }
            for (const ICVar* const cvar : ListedByPrefix(group.prefix)) {
                if (ContainsIgnoreCase(cvar->GetName(), query)) {
                    return true;
                }
            }
            return false;
        };
        const auto familyMatches = [&groupMatches](const Family& family) {
            return std::any_of(family.groups.begin(), family.groups.end(), groupMatches);
        };

        // CVar のまとまり（チームで共有する設定と、自分だけの設定に分ける）
        for (const bool personal : { false, true }) {
            const bool anyFamily = std::any_of(families_.begin(), families_.end(),
                [&](const Family& family) { return family.personal == personal && familyMatches(family); });
            if (!anyFamily) {
                continue;
            }

            const bool headerOpen = ImGui::TreeNodeEx(personal ? "個人の作業状態##personal" : "プロジェクト設定##shared",
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s へ自動保存（%s）", personal ? kPersonalPath : kSharedPath,
                    personal ? "自分だけ" : "チームで共有");
            }
            if (!headerOpen) {
                continue;
            }

            for (const Family& family : families_) {
                if (family.personal != personal || !familyMatches(family)) {
                    continue;
                }
                const std::string familyId = family.label + "##family";
                if (!ImGui::TreeNodeEx(familyId.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    continue;
                }
                for (const Group& group : family.groups) {
                    if (!groupMatches(group)) {
                        continue;
                    }
                    ImGui::PushID(group.prefix.c_str());
                    const bool selected = selectedSection_.empty() && group.prefix == selectedPrefix_;
                    if (ImGui::Selectable(group.label.c_str(), selected, ImGuiSelectableFlags_SpanAvailWidth)) {
                        selectedPrefix_ = group.prefix;
                        selectedSection_.clear();
                        filter_[0] = '\0';
                    }
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - 30.0f);
                    ImGui::TextColored(Theme::kTextMute, "%zu", group.count);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        // 機能ごとの専用の設定（Shading・Post Effects など）
        auto& registry = Editor::EditorPanelRegistry::Get();
        const bool anySection = registry.Any(Editor::PanelPlacement::SettingsSection,
            [&query](const Editor::EditorPanel& panel) { return ContainsIgnoreCase(panel.Id(), query); });
        if (anySection
            && ImGui::TreeNodeEx("機能の設定##sections", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            registry.ForEach(Editor::PanelPlacement::SettingsSection, [this, &query](Editor::EditorPanel& panel) {
                if (!ContainsIgnoreCase(panel.Id(), query)) {
                    return;
                }
                if (ImGui::Selectable(panel.Id().c_str(), panel.Id() == selectedSection_,
                    ImGuiSelectableFlags_SpanAvailWidth)) {
                    selectedSection_ = panel.Id();
                    filter_[0] = '\0';
                }
                });
            ImGui::TreePop();
        }
    }

    void ProjectSettingsWindow::DrawGroupPane()
    {
        bool personal = false;
        const Group* const group = FindSelectedGroup(&personal);
        if (!group) {
            UI::Hint("左の一覧から項目を選んでください");
            return;
        }

        const std::vector<ICVar*> items = ListedByPrefix(group->prefix);
        const size_t modified = static_cast<size_t>(std::count_if(items.begin(), items.end(),
            [](const ICVar* cvar) { return cvar->IsModified(); }));

        // 見出し：名前・件数・変更件数・既定値へ戻す
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(group->label.c_str());
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::kTextMute, "%zu 項目", items.size());
        if (modified > 0) {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(Theme::kWarm, "変更 %zu 件", modified);
        }

        const float resetWidth = UI::Bar::ButtonWidth("既定値へ戻す");
        ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + 12.0f, ImGui::GetContentRegionMax().x - resetWidth));
        if (UI::Bar::Button("既定値へ戻す", false, "このまとまりの項目をすべて既定値へ戻す（Ctrl+Z で戻せる）",
            modified > 0)) {
            CVarUI::ResetTree(group->prefix);
        }

        ImGui::TextColored(Theme::kTextMute, "%s*  →  %s", group->prefix.c_str(), personal ? kPersonalPath : kSharedPath);
        ImGui::Separator();
        ImGui::Spacing();

        CVarUI::DrawTree(group->prefix);
    }

    void ProjectSettingsWindow::DrawSectionPane()
    {
        Editor::EditorPanel* const panel = Editor::EditorPanelRegistry::Get().Find(selectedSection_);
        if (!panel || panel->desc.placement != Editor::PanelPlacement::SettingsSection || !panel->desc.draw) {
            selectedSection_.clear();
            UI::Hint("左の一覧から項目を選んでください");
            return;
        }

        ImGui::TextColored(Theme::kTextMute, "機能の設定  /");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(panel->Id().c_str());
        ImGui::Separator();
        ImGui::Spacing();
        panel->desc.draw();
    }

    void ProjectSettingsWindow::DrawSearchResults()
    {
        const std::string_view query = filter_;
        size_t hits = 0;

        for (const Family& family : families_) {
            for (const Group& group : family.groups) {
                std::vector<ICVar*> matched;
                for (ICVar* const cvar : ListedByPrefix(group.prefix)) {
                    const char* const description = cvar->GetDescription();
                    if (ContainsIgnoreCase(cvar->GetName(), query)
                        || (description && ContainsIgnoreCase(description, query))) {
                        matched.push_back(cvar);
                    }
                }
                if (matched.empty()) {
                    continue;
                }

                hits += matched.size();
                ImGui::SeparatorText(group.prefix.c_str());
                for (ICVar* const cvar : matched) {
                    CVarUI::DrawWidget(cvar);
                }
            }
        }

        if (hits == 0) {
            UI::Hint("一致する項目がありません");
        }
    }

    void ProjectSettingsWindow::DrawFooter()
    {
        size_t modified = 0;
        for (const ICVar* const cvar : CVarRegistry::Get().GetAll()) {
            if (IsListed(cvar) && cvar->IsModified()) {
                ++modified;
            }
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(modified > 0 ? Theme::kWarm : Theme::kTextMute, "既定値から変えた項目 %zu 件", modified);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::kTextMute, "変えるとその場で保存");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("チームで共有: %s\n自分だけ（d.）: %s", kSharedPath, kPersonalPath);
        }
    }
}

#endif // CORE_EDITOR
