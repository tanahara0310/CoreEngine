#include "pch.h"
#include "EngineSystem/Settings/ProjectSettings.h"

#include "Utility/Path/ProjectPaths.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <filesystem>
#include <fstream>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kSettingsPath = "Application/Config/EngineSettings/Project.json";
        constexpr const char* kNameKey = "name";
        constexpr const char* kInitialSceneKey = "initialScene";
        constexpr const char* kVersionKey = "version";
        constexpr const char* kVersion = "1.0";
    }

    ProjectSettings& ProjectSettings::Get()
    {
        static ProjectSettings instance;
        return instance;
    }

    std::string ProjectSettings::GetProjectName() const
    {
        if (!name_.empty()) {
            return name_;
        }
        const std::u8string folder = ProjectPaths::ProjectRoot().filename().u8string();
        return std::string(folder.begin(), folder.end());
    }

    void ProjectSettings::Reload()
    {
        name_.clear();
        initialSceneName_.clear();

        const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return;
        }

        // ここはログの初期化より前に走りうるので、壊れていても黙って既定へ倒す
        nlohmann::json root = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (root.is_discarded() || !root.is_object()) {
            return;
        }
        if (const auto found = root.find(kNameKey);
            found != root.end() && found->is_string()) {
            name_ = found->get<std::string>();
        }
        if (const auto found = root.find(kInitialSceneKey);
            found != root.end() && found->is_string()) {
            initialSceneName_ = found->get<std::string>();
        }
    }

    bool ProjectSettings::SetInitialSceneName(std::string sceneName)
    {
        if (initialSceneName_ == sceneName) {
            return true;
        }
        initialSceneName_ = std::move(sceneName);
        return Save();
    }

    bool ProjectSettings::Save() const
    {
        const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }

        // ファイルにある項目は残し、この設定が持つ項目だけを書き換える
        nlohmann::json root = nlohmann::json::object();
        if (std::ifstream in(path, std::ios::binary); in) {
            nlohmann::json existing = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (!existing.is_discarded() && existing.is_object()) {
                root = std::move(existing);
            }
        }
        root[kVersionKey] = kVersion;
        root[kInitialSceneKey] = initialSceneName_;

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << root.dump(4) << '\n';
        return static_cast<bool>(out);
    }
}
