#include "pch.h"
#include "Scene/DataScene.h"

#include "Scene/Feature/ISceneFeature.h"
#include "Scene/Feature/SceneFeatureRegistry.h"
#include "Scene/SceneSaveSystem.h"
#include "Utility/Logger/Logger.h"

#include <memory>
#include <utility>

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
                    "シーン {} の Feature {} は登録されていないので、足しません", sceneName_, name);
                continue;
            }
            AddFeature(std::move(feature));
        }
    }
}
