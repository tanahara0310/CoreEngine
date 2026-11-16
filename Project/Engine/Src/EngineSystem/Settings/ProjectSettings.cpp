#include "pch.h"
#include "EngineSystem/Settings/ProjectSettings.h"

#include "Utility/Algorithm/AssignIfChanged.h"
#include "Utility/JsonManager/JsonFields.h"
#include "Utility/JsonManager/JsonManager.h"
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
        constexpr const char* kLoadingScreenShaderKey = "loadingScreenShader";
        constexpr const char* kLoadingScreenImagesKey = "loadingScreenImages";
        constexpr const char* kPostEffectsKey = "postEffects";
        constexpr const char* kDefaultTransitionKey = "defaultTransition";
        constexpr const char* kExportExcludedScenesKey = "exportExcludedScenes";

        /// @brief ポストエフェクト 1 つの項目（欄があるときだけ読む）
        constexpr auto kPostEffectFields = [](auto& effect, auto&& field) {
            field("name", effect.name, JsonFields::kIfPresent);
            field("material", effect.material, JsonFields::kIfPresent);
            field("stage", effect.stage, JsonFields::kIfPresent);
            field("enabled", effect.enabled, JsonFields::kIfPresent);
        };

        /// @brief 既定の遷移を読む（無ければ specified が false のまま）
        ProjectTransitionSettings ReadDefaultTransition(const nlohmann::json& root)
        {
            ProjectTransitionSettings transition;
            const auto found = root.find(kDefaultTransitionKey);
            if (found == root.end() || !found->is_object()) {
                return transition;
            }
            transition.specified = true;
            transition.type = found->value("type", transition.type);
            transition.effect = found->value("effect", transition.effect);
            transition.outSeconds = found->value("outSeconds", transition.outSeconds);
            transition.inSeconds = found->value("inSeconds", transition.inSeconds);
            transition.hideUI = found->value("hideUI", transition.hideUI);
            transition.duckBgm = found->value("duckBgm", transition.duckBgm);
            return transition;
        }

        /// @brief 設定のファイルを読む（無いか読めなければ空のオブジェクト）
        nlohmann::json ReadSettingsFile(const std::filesystem::path& path)
        {
            if (std::ifstream in(path, std::ios::binary); in) {
                nlohmann::json existing = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
                if (!existing.is_discarded() && existing.is_object()) {
                    return existing;
                }
            }
            return nlohmann::json::object();
        }

        /// @brief 設定のファイルを書く（フォルダが無ければ作る）
        bool WriteSettingsFile(const std::filesystem::path& path, const nlohmann::json& root)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                return false;
            }
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            out << root.dump(4) << '\n';
            return static_cast<bool>(out);
        }

        /// @brief ポストエフェクトの並びを読む（名前かマテリアルの無い要素は飛ばす）
        std::vector<ProjectPostEffectSettings> ReadPostEffects(const nlohmann::json& root)
        {
            std::vector<ProjectPostEffectSettings> effects;
            const auto found = root.find(kPostEffectsKey);
            if (found == root.end() || !found->is_array()) {
                return effects;
            }
            for (const nlohmann::json& entry : *found) {
                if (!entry.is_object()) {
                    continue;
                }
                ProjectPostEffectSettings effect;
                effect.stage = "PostTonemap";
                JsonFields::ReadFields(entry, effect, kPostEffectFields);
                if (!effect.name.empty() && !effect.material.empty()) {
                    effects.push_back(std::move(effect));
                }
            }
            return effects;
        }

        /// @brief 文字列の配列の項目を読む（無い・配列でないときは空。空の文字列は飛ばす）
        void ReadItem(const nlohmann::json& root, const char* key, std::vector<std::string>& values)
        {
            values.clear();
            const auto found = root.find(key);
            if (found == root.end() || !found->is_array()) {
                return;
            }
            for (const nlohmann::json& value : *found) {
                if (value.is_string() && !value.get_ref<const std::string&>().empty()) {
                    values.push_back(value.get<std::string>());
                }
            }
        }

        /// @brief 文字列の項目を読む（無い・文字列でないときは空）
        void ReadItem(const nlohmann::json& root, const char* key, std::string& value)
        {
            const auto found = root.find(key);
            value = (found != root.end() && found->is_string()) ? found->get<std::string>() : std::string{};
        }

        /// @brief 文字列か文字列の配列の項目を書く（空なら項目ごと消す）
        template <class Value>
        void WriteItem(nlohmann::json& root, const char* key, const Value& value)
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

    template <class Self, class Visitor>
    void ProjectSettings::VisitOptionalFields(Self& self, Visitor&& visit)
    {
        visit(kSplashImageKey, self.splashImage_);
        visit(kSplashTitleKey, self.splashTitle_);
        visit(kSplashSubtitleKey, self.splashSubtitle_);
        visit(kSplashTipsKey, self.splashTips_);
        visit(kLoadingScreenShaderKey, self.loadingScreenShader_);
        visit(kLoadingScreenImagesKey, self.loadingScreenImages_);
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
        VisitOptionalFields(*this, [](const char*, auto& value) { value.clear(); });
        postEffects_.clear();
        defaultTransition_ = ProjectTransitionSettings{};
        exportExcludedScenes_.clear();

        // ここはログの初期化より前に走りうるので、無くても壊れていても黙って既定へ倒す
        const nlohmann::json root = JsonManager::ParseFile(ProjectPaths::Resolve(kSettingsPath));
        if (root.is_discarded() || !root.is_object()) {
            return;
        }
        ReadItem(root, kNameKey, name_);
        ReadItem(root, kInitialSceneKey, initialSceneName_);
        VisitOptionalFields(*this, [&root](const char* key, auto& value) { ReadItem(root, key, value); });
        postEffects_ = ReadPostEffects(root);
        defaultTransition_ = ReadDefaultTransition(root);
        ReadItem(root, kExportExcludedScenesKey, exportExcludedScenes_);
    }

    bool ProjectSettings::SetInitialSceneName(std::string sceneName)
    {
        if (!AssignIfChanged(initialSceneName_, std::move(sceneName))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetSplashImage(std::string path)
    {
        if (!AssignIfChanged(splashImage_, std::move(path))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetSplashTitle(std::string title)
    {
        if (!AssignIfChanged(splashTitle_, std::move(title))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetSplashSubtitle(std::string subtitle)
    {
        if (!AssignIfChanged(splashSubtitle_, std::move(subtitle))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetSplashTips(std::vector<std::string> tips)
    {
        if (!AssignIfChanged(splashTips_, std::move(tips))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetLoadingScreenShader(std::string path)
    {
        if (!AssignIfChanged(loadingScreenShader_, std::move(path))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetLoadingScreenImages(std::vector<std::string> images)
    {
        if (!AssignIfChanged(loadingScreenImages_, std::move(images))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::SetExportExcludedScenes(std::vector<std::string> scenes)
    {
        if (!AssignIfChanged(exportExcludedScenes_, std::move(scenes))) {
            return true;
        }
        return Save();
    }

    bool ProjectSettings::AddPostEffect(const ProjectPostEffectSettings& effect)
    {
        const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
        nlohmann::json root = ReadSettingsFile(path);
        nlohmann::json& list = root[kPostEffectsKey];
        if (!list.is_array()) {
            list = nlohmann::json::array();
        }
        nlohmann::json entry = nlohmann::json::object();
        JsonFields::WriteFields(entry, effect, kPostEffectFields);
        list.push_back(std::move(entry));
        if (!WriteSettingsFile(path, root)) {
            return false;
        }
        postEffects_.push_back(effect);
        return true;
    }

    bool ProjectSettings::Save() const
    {
        const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);

        // ファイルにある項目は残し、この設定が持つ項目だけを書き換える
        nlohmann::json root = ReadSettingsFile(path);
        root[kVersionKey] = kVersion;
        root[kInitialSceneKey] = initialSceneName_;
        VisitOptionalFields(*this, [&root](const char* key, const auto& value) { WriteItem(root, key, value); });
        WriteItem(root, kExportExcludedScenesKey, exportExcludedScenes_);
        return WriteSettingsFile(path, root);
    }
}
