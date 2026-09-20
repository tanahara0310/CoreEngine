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
        // 中身は保存データが決める。Feature・既定の床・衝突マトリクスは BaseScene が当てる
        SetSceneName(sceneName_);
    }
}
