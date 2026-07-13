#include "pch.h"
#include "SceneBGMFeature.h"
#include "Scene/SceneManager.h"

namespace CoreEngine
{
    void SceneBGMFeature::RegisterSceneBGM(SceneContext& ctx, std::unique_ptr<SoundManager::SoundResource>* bgm)
    {
        sceneBGM_ = bgm;

        // 現在設定されているBGM音量を取得
        if (bgm && *bgm && (*bgm)->IsValid()) {
            baseBGMVolume_ = (*bgm)->GetVolume();
        }

        // SceneManager経由でBGMコールバックを登録
        if (ctx.sceneManager) {
            ctx.sceneManager->RegisterSceneBGMCallback([this](float volumeMultiplier) {
                if (sceneBGM_ && *sceneBGM_ && (*sceneBGM_)->IsValid()) {
                    // 基本音量 × トランジション倍率で音量を設定
                    (*sceneBGM_)->SetVolume(baseBGMVolume_ * volumeMultiplier);
                }
                });
        }
    }
}
