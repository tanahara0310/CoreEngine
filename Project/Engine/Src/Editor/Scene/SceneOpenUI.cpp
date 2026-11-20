#include "pch.h"
#include "Editor/Scene/SceneOpenUI.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Scene/LastOpenedScene.h"
#include "Scene/SceneSaveSystem.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <unordered_map>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr std::size_t kMenuRecentCount = 3;
        constexpr float kVisibleRows = 14.0f;
        constexpr const char* kQuickOpenPopupId = "##SceneQuickOpen";
        constexpr const char* kQuickOpenShortcut = "Ctrl+O";

        /// @brief 最近開いたシーンのうち、今あって今のシーンでないもの（新しい順）
        std::vector<std::string> ExistingRecent(const std::vector<std::string>& sceneNames,
            const std::string& currentScene, std::size_t maxCount)
        {
            std::vector<std::string> result;
            for (std::string& name : LastOpenedScene::LoadRecent()) {
                if (result.size() >= maxCount) {
                    break;
                }
                if (name != currentScene && std::find(sceneNames.begin(), sceneNames.end(), name) != sceneNames.end()) {
                    result.push_back(std::move(name));
                }
            }
            return result;
        }

        /// @brief 日付の短い表し方（今年なら 月/日、ほかの年なら 年/月/日）
        std::string FormatDate(const std::optional<std::filesystem::file_time_type>& time)
        {
            if (!time) {
                return {};
            }
            const auto system = std::chrono::clock_cast<std::chrono::system_clock>(*time);
            const std::time_t seconds = std::chrono::system_clock::to_time_t(system);
            const std::time_t nowSeconds = std::time(nullptr);
            std::tm local{};
            std::tm now{};
            if (localtime_s(&local, &seconds) != 0 || localtime_s(&now, &nowSeconds) != 0) {
                return {};
            }
            char text[16] = {};
            if (local.tm_year == now.tm_year) {
                std::snprintf(text, sizeof(text), "%02d/%02d", local.tm_mon + 1, local.tm_mday);
            } else {
                std::snprintf(text, sizeof(text), "%04d/%02d/%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
            }
            return text;
        }

        /// @brief 今のフォントで描いたときの幅（丸めない）
        float TextWidth(std::string_view text)
        {
            return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f,
                text.data(), text.data() + text.size()).x;
        }

        /// @brief 当たった字だけ色を変えて名前を描く
        void DrawHighlighted(ImDrawList* drawList, ImVec2 pos, std::string_view text,
            const std::vector<std::pair<std::size_t, std::size_t>>& ranges, ImU32 normal, ImU32 hit)
        {
            ImFont* const font = ImGui::GetFont();
            const float size = ImGui::GetFontSize();
            const auto draw = [&](std::size_t begin, std::size_t end, ImU32 color) {
                if (begin >= end) {
                    return;
                }
                const char* const first = text.data() + begin;
                const char* const last = text.data() + end;
                drawList->AddText(font, size, pos, color, first, last);
                pos.x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, first, last).x;
            };

            std::size_t at = 0;
            for (const auto& [start, length] : ranges) {
                draw(at, start, normal);
                draw(start, start + length, hit);
                at = start + length;
            }
            draw(at, text.size(), normal);
        }
    }

    // ===== File > シーンを開く =====

    std::string SceneOpenMenu::Draw(const std::vector<std::string>& sceneNames, const std::string& currentScene)
    {
        // 親のメニューの「シーンを開く」の行
        const ImRect opener(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

        if (ImGui::IsWindowAppearing()) {
            filter_[0] = '\0';
            recent_ = ExistingRecent(sceneNames, currentScene, kMenuRecentCount);
            entries_ = SceneNameList::Group(sceneNames);
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
        const bool enter = ImGui::InputTextWithHint("##sceneFilter", "名前で絞り込む", filter_, sizeof(filter_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll);

        // 何も打たずに親のメニューのほかの行を指したら欄から外れる（外れるとこのメニューが閉じられる）
        if (filter_[0] == '\0' && ImGui::IsItemActive() && !opener.Contains(ImGui::GetIO().MousePos)
            && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            ImGui::ClearActiveID();
        }

        std::string chosen;

        // 絞り込み中は当たったシーンだけを並べる
        if (!SceneNameList::IsBlank(filter_)) {
            const std::vector<SceneNameList::Hit> hits = SceneNameList::Search(filter_, sceneNames);
            ImGui::SeparatorText((std::to_string(hits.size()) + " 件").c_str());
            if (hits.empty()) {
                ImGui::MenuItem("当たるシーンがありません", nullptr, false, false);
            }
            ImGui::PushID("hits");
            for (const SceneNameList::Hit& hit : hits) {
                if (DrawSceneItem(hit.name, hit.name.c_str(), currentScene)) {
                    chosen = hit.name;
                }
            }
            ImGui::PopID();
            if (enter && !hits.empty()) {
                chosen = hits.front().name;
                ImGui::CloseCurrentPopup();
            }
            return chosen;
        }

        if (!recent_.empty()) {
            ImGui::SeparatorText("最近開いた");
            ImGui::PushID("recent");
            for (const std::string& name : recent_) {
                if (DrawSceneItem(name, name.c_str(), currentScene)) {
                    chosen = name;
                }
            }
            ImGui::PopID();
        }

        ImGui::SeparatorText(("すべて（" + std::to_string(sceneNames.size()) + "）").c_str());
        if (entries_.empty()) {
            ImGui::MenuItem("シーンがありません", nullptr, false, false);
        }

        const float fontSize = ImGui::GetFontSize();
        const float spaceWidth = std::max(1.0f, TextWidth(" "));
        ImGui::PushID("all");
        for (const SceneNameList::Entry& entry : entries_) {
            if (!entry.IsGroup()) {
                const std::string& name = entry.scenes.front();
                if (DrawSceneItem(name, name.c_str(), currentScene)) {
                    chosen = name;
                }
                continue;
            }

            // 群の行：名前の後ろを空けて、右の矢印の手前に数を描く
            const std::string count = std::to_string(entry.scenes.size());
            const float countWidth = TextWidth(count);
            const int padding = static_cast<int>(std::ceil((countWidth + fontSize) / spaceWidth));
            const std::string label = entry.group + std::string(static_cast<std::size_t>(padding), ' ') + "##group";
            const bool hasCurrent = std::find(entry.scenes.begin(), entry.scenes.end(), currentScene) != entry.scenes.end();

            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImVec2 labelPos = ImGui::GetCursorScreenPos();
            const float contentRight = labelPos.x + ImGui::GetContentRegionAvail().x;
            const bool open = ImGui::BeginMenu(label.c_str());

            const float countLeft = labelPos.x + TextWidth(entry.group) + fontSize * 0.5f;
            const float countRight = contentRight - fontSize * 1.5f;
            drawList->AddText(ImVec2(std::max(countLeft, countRight - countWidth), labelPos.y),
                ImGui::GetColorU32(hasCurrent ? Theme::kAccentHover : Theme::kTextMute), count.c_str());

            if (open) {
                for (const std::string& name : entry.scenes) {
                    const std::string shortName(SceneNameList::ShortName(name, entry.group));
                    if (DrawSceneItem(name, shortName.c_str(), currentScene)) {
                        chosen = name;
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::PopID();
        return chosen;
    }

    bool SceneOpenMenu::DrawSceneItem(const std::string& sceneName, const char* label, const std::string& currentScene)
    {
        ImGui::PushID(sceneName.c_str());
        const bool pressed = ImGui::MenuItem(label, nullptr, sceneName == currentScene);
        ImGui::PopID();
        return pressed;
    }

    // ===== Ctrl+O の欄 =====

    void SceneQuickOpen::Open(const std::vector<std::string>& sceneNames, const std::string& currentScene)
    {
        std::unordered_map<std::string, std::string> groupOf;
        for (const SceneNameList::Entry& entry : SceneNameList::Group(sceneNames)) {
            if (!entry.IsGroup()) {
                continue;
            }
            for (const std::string& name : entry.scenes) {
                groupOf[name] = entry.group;
            }
        }

        // 空の絞り込みで探すと全部が名前順に並ぶ
        scenes_.clear();
        for (SceneNameList::Hit& hit : SceneNameList::Search({}, sceneNames)) {
            Scene scene;
            scene.name = std::move(hit.name);
            if (const auto it = groupOf.find(scene.name); it != groupOf.end()) {
                scene.group = it->second;
            }
            scene.date = FormatDate(SceneSaveSystem::FindLastWriteTime(scene.name));
            scenes_.push_back(std::move(scene));
        }

        recent_.clear();
        for (const std::string& name : ExistingRecent(sceneNames, currentScene, scenes_.size())) {
            const auto it = std::find_if(scenes_.begin(), scenes_.end(), [&name](const Scene& scene) { return scene.name == name; });
            if (it != scenes_.end()) {
                recent_.push_back(static_cast<std::size_t>(it - scenes_.begin()));
            }
        }

        currentScene_ = currentScene;
        query_[0] = '\0';
        Rebuild();
        openRequested_ = true;
    }

    void SceneQuickOpen::Rebuild()
    {
        builtQuery_ = query_;
        rows_.clear();
        selected_ = 0;
        scrollToSelected_ = true;

        // 空なら、最近開いたシーンを先に、残りを名前順に
        if (SceneNameList::IsBlank(query_)) {
            std::vector<bool> listed(scenes_.size(), false);
            for (const std::size_t index : recent_) {
                rows_.push_back({ index, {}, true });
                listed[index] = true;
            }
            for (std::size_t index = 0; index < scenes_.size(); ++index) {
                if (!listed[index]) {
                    rows_.push_back({ index, {}, false });
                }
            }
            return;
        }

        for (std::size_t index = 0; index < scenes_.size(); ++index) {
            if (std::optional<SceneNameList::Match> match = SceneNameList::Find(query_, scenes_[index].name)) {
                rows_.push_back({ index, std::move(*match), false });
            }
        }
        std::stable_sort(rows_.begin(), rows_.end(),
            [](const Row& a, const Row& b) { return a.match.score > b.match.score; });
    }

    int SceneQuickOpen::OnInputEvent(ImGuiInputTextCallbackData* data)
    {
        auto* const self = static_cast<SceneQuickOpen*>(data->UserData);
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || self->rows_.empty()) {
            return 0;
        }
        const int count = static_cast<int>(self->rows_.size());
        const int step = (data->EventKey == ImGuiKey_UpArrow) ? -1 : 1;
        self->selected_ = (self->selected_ + step + count) % count;
        self->scrollToSelected_ = true;
        return 0;
    }

    std::string SceneQuickOpen::Draw()
    {
        const bool appearing = openRequested_;
        if (openRequested_) {
            ImGui::OpenPopup(kQuickOpenPopupId);
            openRequested_ = false;
        }

        // メインの窓の上の真ん中に出す
        const ImGuiViewport* const viewport = ImGui::GetMainViewport();
        const float fontSize = ImGui::GetFontSize();
        const float width = std::min(fontSize * 38.0f, viewport->WorkSize.x - fontSize * 2.0f);
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + fontSize * 4.0f),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
        ImGui::SetNextWindowViewport(viewport->ID);
        if (!ImGui::BeginPopup(kQuickOpenPopupId, ImGuiWindowFlags_NoMove)) {
            return {};
        }

        // 字が空のときの Esc で閉じる（字があれば欄が消す）
        if (!appearing && query_[0] == '\0' && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return {};
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float shortcutWidth = ImGui::CalcTextSize(kQuickOpenShortcut).x;
        if (appearing) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - shortcutWidth - style.ItemSpacing.x);
        const bool enter = ImGui::InputTextWithHint("##query", "シーンの名前を打つ（字が続いていなくても当たる）",
            query_, sizeof(query_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackHistory,
            &SceneQuickOpen::OnInputEvent, this);
        ImGui::SameLine();
        ImGui::TextColored(Theme::kTextMute, "%s", kQuickOpenShortcut);

        if (builtQuery_ != query_) {
            Rebuild();
        }

        std::string chosen;
        if (enter && !rows_.empty()) {
            chosen = scenes_[rows_[static_cast<std::size_t>(selected_)].scene].name;
        }

        float dateWidth = 0.0f;
        for (const Scene& scene : scenes_) {
            dateWidth = std::max(dateWidth, TextWidth(scene.date));
        }

        // 一覧（長ければ欄の中で巻く）
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, rowHeight * kVisibleRows + rowHeight * 2.0f));
        if (ImGui::BeginChild("##rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY)) {
            const bool blank = SceneNameList::IsBlank(query_);
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            if (rows_.empty()) {
                ImGui::TextColored(Theme::kTextMute, "「%s」に当たるシーンがありません", query_);
            }
            for (std::size_t index = 0; index < rows_.size(); ++index) {
                if (blank && index == 0 && rows_[index].recent) {
                    ImGui::TextColored(Theme::kTextMute, "最近開いた");
                }
                if (blank && !rows_[index].recent && (index == 0 || rows_[index - 1].recent)) {
                    ImGui::TextColored(Theme::kTextMute, "すべて");
                }
                if (DrawRow(index, right, dateWidth)) {
                    chosen = scenes_[rows_[index].scene].name;
                }
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        const float footerRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::TextColored(Theme::kTextMute, "↑↓ 選ぶ　　Enter 開く　　Esc 消す・閉じる");
        const std::string count = SceneNameList::IsBlank(query_)
            ? std::to_string(scenes_.size()) + " 件"
            : std::to_string(rows_.size()) + " / " + std::to_string(scenes_.size()) + " 件";
        ImGui::SameLine(footerRight - ImGui::CalcTextSize(count.c_str()).x);
        ImGui::TextColored(Theme::kTextMute, "%s", count.c_str());

        if (!chosen.empty()) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return chosen;
    }

    bool SceneQuickOpen::DrawRow(std::size_t index, float right, float dateWidth)
    {
        const Row& row = rows_[index];
        const Scene& scene = scenes_[row.scene];
        const bool selected = static_cast<int>(index) == selected_;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float fontSize = ImGui::GetFontSize();
        const float lineHeight = ImGui::GetTextLineHeight();

        ImGui::PushID(static_cast<int>(index));
        const bool pressed = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2(0.0f, lineHeight));
        ImGui::PopID();

        // マウスを動かしたときだけ、指した行を選ぶ
        const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
        if (ImGui::IsItemHovered() && (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)) {
            selected_ = static_cast<int>(index);
        }
        if (selected && scrollToSelected_) {
            if (index == 0) {
                ImGui::SetScrollY(0.0f);
            } else {
                ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
            }
            scrollToSelected_ = false;
        }

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        DrawHighlighted(drawList, pos, scene.name, row.match.ranges,
            ImGui::GetColorU32(Theme::kText), ImGui::GetColorU32(Theme::kAccentHover));
        float used = pos.x + TextWidth(scene.name);

        // 今開いているシーンの印
        if (scene.name == currentScene_) {
            constexpr const char* kOpenTag = "開いている";
            const float tagLeft = used + fontSize * 0.5f;
            const float tagWidth = TextWidth(kOpenTag) + fontSize * 0.6f;
            drawList->AddRect(ImVec2(tagLeft, pos.y), ImVec2(tagLeft + tagWidth, pos.y + lineHeight),
                ImGui::GetColorU32(Theme::kOutline), 3.0f);
            drawList->AddText(ImVec2(tagLeft + fontSize * 0.3f, pos.y), ImGui::GetColorU32(Theme::kOk), kOpenTag);
            used = tagLeft + tagWidth;
        }

        // 右から日付と群の名前
        const ImU32 muted = ImGui::GetColorU32(Theme::kTextMute);
        if (!scene.date.empty()) {
            drawList->AddText(ImVec2(right - TextWidth(scene.date), pos.y), muted, scene.date.c_str());
        }
        if (!scene.group.empty()) {
            const float groupLeft = right - dateWidth - fontSize * 1.5f - TextWidth(scene.group);
            if (groupLeft > used + fontSize) {
                drawList->AddText(ImVec2(groupLeft, pos.y), muted, scene.group.c_str());
            }
        }
        return pressed;
    }
}

#endif // CORE_EDITOR
