#pragma once

#include "ISceneFeature.h"
#include "Audio/SoundManager.h"
#include <memory>

namespace CoreEngine
{
    /// @brief シーン BGM のトランジション時自動フェードを管理する Feature
    class SceneBGMFeature : public ISceneFeature {
    public:
        const char* GetName() const override { return "SceneBGM"; }

        /// @brief シーンBGMを登録し、トランジション時の自動フェードを有効化
        /// @param bgm BGMのSoundResourceポインタ（現在のSetVolume()で設定した音量が使用されます）
        void RegisterSceneBGM(SceneContext& ctx, std::unique_ptr<SoundManager::SoundResource>* bgm);

    private:
        // BGM管理用
        std::unique_ptr<SoundManager::SoundResource>* sceneBGM_ = nullptr;
        float baseBGMVolume_ = 1.0f;
    };
}
