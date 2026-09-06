#include "pch.h"
#include "CameraSequence.h"

namespace CoreEngine
{
    bool CameraSequence::Play(const std::string& name, const CameraSequencePlaybackOptions& options)
    {
        if (!activePlayer_) {
            return false;
        }

        auto asset = library_.Get(name);
        if (!asset) {
            return false;
        }

        activePlayer_->Play(std::move(asset), options, name);
        return activePlayer_->IsActive();
    }

    void CameraSequence::Stop()
    {
        if (activePlayer_) {
            activePlayer_->Stop();
        }
    }

    void CameraSequence::Pause()
    {
        if (activePlayer_) {
            activePlayer_->Pause();
        }
    }

    void CameraSequence::Resume()
    {
        if (activePlayer_) {
            activePlayer_->Resume();
        }
    }

    bool CameraSequence::IsPlaying()
    {
        return activePlayer_ && activePlayer_->IsPlaying();
    }

    bool CameraSequence::IsActive()
    {
        return activePlayer_ && activePlayer_->IsActive();
    }

    std::string CameraSequence::GetPlayingName()
    {
        return activePlayer_ ? activePlayer_->GetName() : std::string{};
    }
}
