#include "pch.h"
#include "Scene/DataScene.h"

#include "Scene/Feature/ISceneFeature.h"
#include "Scene/Feature/SceneFeatureRegistry.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/Logger/Logger.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    /// @brief 名前を「, 」でつなぐ
    std::string JoinNames(const std::vector<std::string>& names)
    {
        std::string text;
        for (const std::string& name : names) {
            if (!text.empty()) {
                text += ", ";
            }
            text += name;
        }
        return text;
    }
}

namespace CoreEngine
{
    DataScene::DataScene(std::string sceneName)
        : sceneName_(std::move(sceneName))
    {
    }

    void DataScene::OnInitialize()
    {
        SetSceneName(sceneName_);

        const SceneSaveSystem::ManifestSettings settings = SceneSaveSystem::LoadManifestSettings(sceneName_);
        if (settings.defaultGround) {
            SetDefaultGroundEnabled(*settings.defaultGround);
        }
        for (const std::string& name : settings.features) {
            std::unique_ptr<ISceneFeature> feature = SceneFeatureRegistry::Create(name);
            if (!feature) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "シーン {} の Feature {} は登録されていないので、足しません（足せるもの: {}）",
                    sceneName_, name, JoinNames(SceneFeatureRegistry::GetNames()));
                continue;
            }
            if (FindFeature(feature->GetName())) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "シーン {} の Feature {} は既にあるので、足しません", sceneName_, name);
                continue;
            }
            AddFeature(std::move(feature));
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
                "シーン {} に Feature {} を足しました", sceneName_, name);
        }
    }
}
