#include "pch.h"
#include "EngineSystem/PlaybackState.h"

#include "Utility/FrameRate/Time.h"

#include <utility>

namespace CoreEngine
{
    PlaybackStateManager& PlaybackStateManager::GetInstance()
    {
        static PlaybackStateManager instance;
        return instance;
    }

    PlaybackStateManager::PlaybackStateManager()
    {
#ifndef CORE_EDITOR
        // エディタの無いビルドは最初から再生する
        inPlayMode_ = true;
#endif
        SyncTime();
    }

    PlaybackState PlaybackStateManager::GetState() const
    {
        if (!inPlayMode_) {
            return PlaybackState::Editing;
        }
        return (pauseToggled_ && !stepping_) ? PlaybackState::Paused : PlaybackState::Playing;
    }

    void PlaybackStateManager::Play()
    {
        stopRequested_ = false;
        if (!inPlayMode_) {
            playRequested_ = true;
        }
    }

    void PlaybackStateManager::Stop()
    {
        playRequested_ = false;
        if (inPlayMode_) {
            stopRequested_ = true;
        }
    }

    void PlaybackStateManager::TogglePause()
    {
        pauseToggled_ = !pauseToggled_;
        SyncTime();
    }

    void PlaybackStateManager::Pause()
    {
        pauseToggled_ = true;
        SyncTime();
    }

    void PlaybackStateManager::RequestStep()
    {
        if (!inPlayMode_) {
            return;
        }
        pauseToggled_ = true;
        stepRequested_ = true;
        SyncTime();
    }

    void PlaybackStateManager::SetTransitionHooks(TransitionHooks hooks)
    {
        hooks_ = std::move(hooks);
    }

    void PlaybackStateManager::ClearTransitionHooks()
    {
        hooks_ = {};
    }

    void PlaybackStateManager::BeginFrame()
    {
        if (stopRequested_) {
            // 編集中へ戻してから控えを戻す。戻せない場面は再生モードのまま次のフレームで試す
            inPlayMode_ = false;
            stepping_ = false;
            stepRequested_ = false;
            SyncTime();
            if (hooks_.afterStop && !hooks_.afterStop()) {
                inPlayMode_ = true;
                SyncTime();
                return;
            }
            stopRequested_ = false;
            return;
        }

        if (playRequested_) {
            if (hooks_.beforePlay && !hooks_.beforePlay()) {
                return;
            }
            playRequested_ = false;
            inPlayMode_ = true;
            SyncTime();
        }

        if (stepRequested_) {
            stepRequested_ = false;
            if (inPlayMode_ && pauseToggled_) {
                stepping_ = true;
                SyncTime();
            }
        }
    }

    void PlaybackStateManager::EndFrame()
    {
        if (!stepping_) {
            return;
        }
        stepping_ = false;
        SyncTime();
    }

    void PlaybackStateManager::SyncTime() const
    {
        Time::SetPaused(!IsAdvancing());
    }
}
