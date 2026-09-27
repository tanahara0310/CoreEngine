#include "pch.h"
#include "SceneEnvironmentIO.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/CVar/CVarScope.h"
#include "Utility/CVar/CVarSerialization.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief シーンフォルダ（SceneSaveSystem がオブジェクトを置く場所と同じ）
        constexpr const char* kSceneRoot = "Application/Assets/Scenes/";

        /// @brief 雲の配置ペイントを持つマネージャーを引く（描画が無い経路では nullptr）
        VolumetricCloudManager* FindCloudManager(EngineSystem* engine)
        {
            auto* domainContext = engine ? engine->GetRenderDomainContext() : nullptr;
            return domainContext ? domainContext->GetVolumetricCloudManager() : nullptr;
        }

        /// @brief 今の見た目を、ファイルに書く形にする（コード既定のままの項目は書かない）
        json BuildRoot()
        {
            json root;
            root["version"] = "1.0";
            CVarSerialization::Save(root, "", /*skipDefaults=*/true, /*excludePrefix=*/{},
                                    CVarScope::Scene);
            return root;
        }
    }

    std::string SceneEnvironmentIO::GetFilePath(const std::string& sceneName)
    {
        return (std::filesystem::path(kSceneRoot) / sceneName / kFileName).string();
    }

    std::string SceneEnvironmentIO::GetCloudPaintFilePath(const std::string& sceneName)
    {
        return (std::filesystem::path(kSceneRoot) / sceneName / "_cloudPaint.bin").string();
    }

    void SceneEnvironmentIO::ResetToDefaults(EngineSystem* engine)
    {
        for (ICVar* cvar : CVarRegistry::Get().GetAll()) {
            if (CVarScopes::IsSceneOwned(cvar->GetName())) {
                cvar->ResetToDefault();
            }
        }

        // 雲の配置ペイントもシーンのもの。書き先を外してから中身を空にする
        //（前のシーンのファイルには触らない）
        if (auto* cloudManager = FindCloudManager(engine)) {
            cloudManager->SetWeatherPaintPath({});
        }
    }

    bool SceneEnvironmentIO::Load(const std::string& sceneName, EngineSystem* engine)
    {
        if (sceneName.empty()) {
            return false;
        }

        // 環境ファイルが無いシーンでも、雲の書き先だけは先に決めておく
        //（エディタでペイントしたらこのシーンへ保存される）
        if (auto* cloudManager = FindCloudManager(engine)) {
            cloudManager->SetWeatherPaintPath(GetCloudPaintFilePath(sceneName));
        }

        // 読んだ中身を、外の変更を見分けるための控えにする（ファイルが無ければ控えも消す）
        SceneSaveSystem::RememberSettingsFile(sceneName, kFileName);

        const std::string path = GetFilePath(sceneName);
        auto& jsonManager = JsonManager::GetInstance();
        if (!jsonManager.FileExists(path)) {
            return false;
        }

        const json root = jsonManager.LoadJson(path);
        if (root.is_null() || !root.is_object() || root.empty()) {
            return false;
        }

        CVarSerialization::Load(root, "", CVarScope::Scene);

        size_t applied = 0;
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (CVarScopes::IsSceneOwned(it.key())) {
                ++applied;
            }
        }
        Logger::GetInstance().Infof(LogCategory::System,
            "シーン {} の環境を読み込みました（{} 件）", sceneName, applied);
        return true;
    }

    bool SceneEnvironmentIO::Save(const std::string& sceneName, bool overwrite)
    {
        if (sceneName.empty()) {
            return false;
        }

        const json root = BuildRoot();
        const SceneSaveSystem::FileWrite action =
            SceneSaveSystem::DecideSettingsFileWrite(sceneName, kFileName, JsonManager::ToFileText(root));
        if (action == SceneSaveSystem::FileWrite::KeepOutside
            || (action == SceneSaveSystem::FileWrite::Conflict && !overwrite)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "シーン {} の見た目のファイルは外で変わったので書きませんでした", sceneName);
            return false;
        }

        const std::string path = GetFilePath(sceneName);
        auto& jsonManager = JsonManager::GetInstance();
        jsonManager.CreateJsonDirectory(std::filesystem::path(path).parent_path().string());
        if (!jsonManager.SaveJson(path, root)) {
            return false;
        }
        SceneSaveSystem::RememberSettingsFile(sceneName, kFileName);
        return true;
    }

    bool SceneEnvironmentIO::HasConflict(const std::string& sceneName)
    {
        return !sceneName.empty()
            && SceneSaveSystem::DecideSettingsFileWrite(sceneName, kFileName, JsonManager::ToFileText(BuildRoot()))
                == SceneSaveSystem::FileWrite::Conflict;
    }

    uint64_t SceneEnvironmentIO::GetChangeRevision()
    {
        uint64_t total = 0;
        for (const ICVar* cvar : CVarRegistry::Get().GetAll()) {
            if (CVarScopes::IsSceneOwned(cvar->GetName())) {
                total += cvar->GetRevision();
            }
        }
        return total;
    }
}
