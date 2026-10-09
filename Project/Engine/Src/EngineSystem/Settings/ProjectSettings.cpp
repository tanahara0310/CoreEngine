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
        constexpr const char* kSplashImageKey = "splashImage";
        constexpr const char* kSplashTitleKey = "splashTitle";
        constexpr const char* kSplashSubtitleKey = "splashSubtitle";
        constexpr const char* kSplashTipsKey = "splashTips";

        /// @brief 文字列の項目を読む（無い・文字列でないときは空）
        std::string ReadString(const nlohmann::json& root, const char* key)
        {
            const auto found = root.find(key);
            return (found != root.end() && found->is_string()) ? found->get<std::string>() : std::string{};
        }

        /// @brief 文字列の項目を書く（空なら項目ごと消す）
        void WriteString(nlohmann::json& root, const char* key, const std::string& value)
        {
            if (value.empty()) {
                root.erase(key);
            } else {
                root[key] = value;
            }
        }
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
        splashImage_.clear();
        splashTitle_.clear();
        splashSubtitle_.clear();
        splashTips_.clear();

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
        splashImage_ = ReadString(root, kSplashImageKey);
        splashTitle_ = ReadString(root, kSplashTitleKey);
        splashSubtitle_ = ReadString(root, kSplashSubtitleKey);
        if (const auto found = root.find(kSplashTipsKey);
            found != root.end() && found->is_array()) {
            for (const nlohmann::json& tip : *found) {
                if (tip.is_string() && !tip.get_ref<const std::string&>().empty()) {
                    splashTips_.push_back(tip.get<std::string>());
                }
            }
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

    bool ProjectSettings::SetSplashImage(std::string path)
    {
        if (splashImage_ == path) {
            return true;
        }
        splashImage_ = std::move(path);
        return Save();
    }

    bool ProjectSettings::SetSplashTitle(std::string title)
    {
        if (splashTitle_ == title) {
            return true;
        }
        splashTitle_ = std::move(title);
        return Save();
    }

    bool ProjectSettings::SetSplashSubtitle(std::string subtitle)
    {
        if (splashSubtitle_ == subtitle) {
            return true;
        }
        splashSubtitle_ = std::move(subtitle);
        return Save();
    }

    bool ProjectSettings::SetSplashTips(std::vector<std::string> tips)
    {
        if (splashTips_ == tips) {
            return true;
        }
        splashTips_ = std::move(tips);
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
        WriteString(root, kSplashImageKey, splashImage_);
        WriteString(root, kSplashTitleKey, splashTitle_);
        WriteString(root, kSplashSubtitleKey, splashSubtitle_);
        if (splashTips_.empty()) {
            root.erase(kSplashTipsKey);
        } else {
            root[kSplashTipsKey] = splashTips_;
        }

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << root.dump(4) << '\n';
        return static_cast<bool>(out);
    }
}
