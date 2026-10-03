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
        mode_ = Mode::Playing;
#endif
        SyncTime();
    }

    PlaybackState PlaybackStateManager::GetState() const
    {
        switch (mode_) {
        case Mode::Editing:
            return PlaybackState::Editing;
        case Mode::Paused:
            return PlaybackState::Paused;
        case Mode::Playing:
        case Mode::Stepping:
        default:
            return PlaybackState::Playing;
        }
    }

    bool PlaybackStateManager::IsPauseToggled() const
    {
        // 編集中は「一時停止で始める」の印、再生モードでは止まっているか（コマ送りの 1 フレームも含む）
        return (mode_ == Mode::Editing) ? startPaused_ : (mode_ == Mode::Paused || mode_ == Mode::Stepping);
    }

    void PlaybackStateManager::Play()
    {
        // 頼みは常に 1 つ。後から来た方が勝つ
        if (request_ == Request::Stop) {
            request_ = Request::None;
        }
        if (mode_ == Mode::Editing) {
            request_ = Request::Play;
        }
    }

    void PlaybackStateManager::Stop()
    {
        if (request_ == Request::Play) {
            request_ = Request::None;
        }
        if (mode_ != Mode::Editing) {
            request_ = Request::Stop;
        }
    }

    void PlaybackStateManager::TogglePause()
    {
        switch (mode_) {
        case Mode::Editing:
            startPaused_ = !startPaused_;
            break;
        case Mode::Playing:
            TransitionTo(Mode::Paused);
            break;
        case Mode::Paused:
        case Mode::Stepping:
            TransitionTo(Mode::Playing);
            break;
        }
    }

    void PlaybackStateManager::Pause()
    {
        if (mode_ == Mode::Editing) {
            startPaused_ = true;
        } else if (mode_ == Mode::Playing) {
            TransitionTo(Mode::Paused);
        }
    }

    void PlaybackStateManager::RequestStep()
    {
        if (mode_ == Mode::Editing) {
            return;
        }
        if (mode_ == Mode::Playing) {
            TransitionTo(Mode::Paused);
        }
        // 停止の頼みが先にあれば、そちらを残す（止めればコマ送りは要らない）
        if (request_ == Request::None) {
            request_ = Request::Step;
        }
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
        switch (request_) {
        case Request::Stop: {
            // 編集中へ戻してから控えを戻す。戻せない場面は再生モードのまま次のフレームで試す
            const bool wasPaused = (mode_ == Mode::Paused || mode_ == Mode::Stepping);
            TransitionTo(Mode::Editing);
            if (hooks_.afterStop && !hooks_.afterStop()) {
                TransitionTo(wasPaused ? Mode::Paused : Mode::Playing);
                return;
            }
            // 一時停止したまま止めたら、次の再生も一時停止で始める
            startPaused_ = wasPaused;
            request_ = Request::None;
            return;
        }

        case Request::Play:
            if (hooks_.beforePlay && !hooks_.beforePlay()) {
                return;
            }
            request_ = Request::None;
            TransitionTo(startPaused_ ? Mode::Paused : Mode::Playing);
            return;

        case Request::Step:
            request_ = Request::None;
            if (mode_ == Mode::Paused) {
                TransitionTo(Mode::Stepping);
            }
            return;

        case Request::None:
        default:
            return;
        }
    }

    void PlaybackStateManager::EndFrame()
    {
        if (mode_ == Mode::Stepping) {
            TransitionTo(Mode::Paused);
        }
    }

    void PlaybackStateManager::TransitionTo(Mode next)
    {
        mode_ = next;
        SyncTime();
    }

    void PlaybackStateManager::SyncTime() const
    {
        Time::SetPaused(Time::DriverKey{}, !IsAdvancing());
    }
}
