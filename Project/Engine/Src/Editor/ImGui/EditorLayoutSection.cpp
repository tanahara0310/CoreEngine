#include "pch.h"
#include "Editor/ImGui/EditorLayoutSection.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/DockingUI.h"
#include "Editor/ImGui/ProjectView.h"
#include "Editor/ImGui/GameDebugUI.h"
#include "Utility/Logger/Logger.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <imgui.h>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief JSON の bool を読む（無ければ fallback）
        bool ReadBool(const nlohmann::json& object, const char* key, bool fallback)
        {
            const auto found = object.find(key);
            return (found != object.end() && found->is_boolean()) ? found->get<bool>() : fallback;
        }
    }

    EditorLayoutSection::EditorLayoutSection(GameDebugUI& gameDebugUI, DockingUI& dockingUI, ProjectView* projectView)
        : gameDebugUI_(&gameDebugUI), dockingUI_(&dockingUI), projectView_(projectView)
    {
    }

    void EditorLayoutSection::Serialize(nlohmann::json& out) const
    {
        if (ImGui::GetCurrentContext()) {
            std::size_t size = 0;
            const char* const settings = ImGui::SaveIniSettingsToMemory(&size);
            lastImGuiSettings_.assign(settings, size);
            ImGui::GetIO().WantSaveIniSettings = false;
        }
        out["imgui"] = lastImGuiSettings_;

        const GameDebugUI::CoreWindows windows = gameDebugUI_->GetCoreWindows();
        nlohmann::json windowsJson = nlohmann::json::object();
        windowsJson["hierarchy"] = windows.hierarchy;
        windowsJson["inspector"] = windows.inspector;
        windowsJson["console"] = windows.console;
        if (projectView_) {
            windowsJson["project"] = projectView_->IsVisible();
        }
        out["windows"] = std::move(windowsJson);

        if (projectView_) {
            nlohmann::json project = nlohmann::json::object();
            project["folder"] = Logger::GetInstance().PathToUtf8(projectView_->GetCurrentFolder());
            project["listView"] = projectView_->IsListView();
            out["project"] = std::move(project);
        }
    }

    void EditorLayoutSection::Deserialize(const nlohmann::json& in)
    {
        // ドックの配置とウィンドウの位置。無ければ標準の配置を組む
        const auto imgui = in.find("imgui");
        if (imgui != in.end() && imgui->is_string() && !imgui->get_ref<const std::string&>().empty()) {
            lastImGuiSettings_ = imgui->get<std::string>();
            ImGui::LoadIniSettingsFromMemory(lastImGuiSettings_.c_str(), lastImGuiSettings_.size());
            dockingUI_->UseSavedLayout();
        } else {
            dockingUI_->RequestResetLayout();
        }

        if (const auto windows = in.find("windows"); windows != in.end() && windows->is_object()) {
            GameDebugUI::CoreWindows state = gameDebugUI_->GetCoreWindows();
            state.hierarchy = ReadBool(*windows, "hierarchy", state.hierarchy);
            state.inspector = ReadBool(*windows, "inspector", state.inspector);
            state.console = ReadBool(*windows, "console", state.console);
            gameDebugUI_->SetCoreWindows(state);
            if (projectView_) {
                projectView_->SetVisible(ReadBool(*windows, "project", projectView_->IsVisible()));
            }
        }

        if (const auto project = in.find("project"); projectView_ && project != in.end() && project->is_object()) {
            if (const auto folder = project->find("folder"); folder != project->end() && folder->is_string()) {
                projectView_->OpenFolder(Logger::GetInstance().Utf8ToPath(folder->get<std::string>()));
            }
            projectView_->SetListView(ReadBool(*project, "listView", projectView_->IsListView()));
        }
    }
}

#endif // CORE_EDITOR
