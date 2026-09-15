#include "pch.h"
#include "Scene/DataScene.h"

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
    }
}
