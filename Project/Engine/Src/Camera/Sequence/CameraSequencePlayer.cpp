#include "pch.h"
#include "CameraSequencePlayer.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    void CameraSequencePlayer::Play(std::shared_ptr<const CameraSequenceAsset> asset,
        const CameraSequencePlaybackOptions& options, const std::string& name)
    {
        // 中身の無いシーケンスで再生状態に入ると、カメラを握ったまま何も出力しない
        // 状態になる（ゲーム側の追従だけが止まる）。停止として扱う。
        if (!asset || asset->keyframes.empty()) {
            Stop();
            return;
        }

        asset_ = std::move(asset);
        options_ = options;
        options_.speed = (options_.speed > 0.0f) ? options_.speed : 1.0f;
        options_.blendInSeconds = (std::max)(options_.blendInSeconds, 0.0f);
        name_ = name;

        state_ = State::Playing;
        time_ = 0.0f;
        blendInElapsed_ = 0.0f;
        playId_ = nextPlayId_++;
        firedEvents_.clear();

        // 発火判定は半開区間 (下限, 今回] なので、下限を負にしておくと
        // 時刻 0 ちょうどのイベントも最初の更新で拾える。
        eventCursor_ = -1.0f;
    }

    void CameraSequencePlayer::Stop()
    {
        asset_.reset();
        name_.clear();
        state_ = State::Stopped;
        time_ = 0.0f;
        blendInElapsed_ = 0.0f;
        playId_ = 0;
        firedEvents_.clear();
        eventCursor_ = -1.0f;
    }

    void CameraSequencePlayer::Pause()
    {
        if (state_ == State::Playing) {
            state_ = State::Paused;
        }
    }

    void CameraSequencePlayer::Resume()
    {
        if (state_ == State::Paused) {
            state_ = State::Playing;
        }
    }

    void CameraSequencePlayer::Update(float scaledDeltaTime, float unscaledDeltaTime)
    {
        firedEvents_.clear();

        // 終端に達した次の更新で停止する。こうすると最後のキーの構図が
        // ちょうど 1 フレーム分カメラへ反映されてから、ゲーム側へ返る。
        if (state_ == State::Finishing) {
            Stop();
            return;
        }

        if (state_ != State::Playing || !asset_) {
            return;
        }

        const float previousTime = time_;

        const float delta = options_.useUnscaledTime ? unscaledDeltaTime : scaledDeltaTime;

        // 繋ぎはポーズの影響を受けさせない。ポーズ中に再生を始めたとき、
        // ブレンドが進まずカメラが動かないまま固まるのを避ける。
        blendInElapsed_ += unscaledDeltaTime;

        AdvanceTime(delta * options_.speed);

        // 巻き戻った（ループした）ときは、終端までと先頭からの 2 区間に分けて拾う。
        // 1 区間として扱うと、跨いだはずのイベントがまるごと落ちる。
        if (time_ < previousTime) {
            CollectEvents(eventCursor_, GetDuration());
            CollectEvents(-1.0f, time_);
        } else {
            CollectEvents(eventCursor_, time_);
        }

        eventCursor_ = time_;
    }

    void CameraSequencePlayer::CollectEvents(float fromTime, float toTime)
    {
        if (!asset_ || asset_->events.empty() || toTime < fromTime) {
            return;
        }

        // events は時刻の昇順に並んでいる前提（CameraSequenceIO::Load が保証する）。
        for (const auto& event : asset_->events) {
            if (event.time > toTime) {
                break;
            }
            if (!event.enabled || event.time <= fromTime) {
                continue;
            }
            firedEvents_.push_back(event);
        }
    }

    void CameraSequencePlayer::AdvanceTime(float delta)
    {
        const float duration = GetDuration();
        if (duration <= 0.0f) {
            return;
        }

        time_ += delta;

        if (time_ < 0.0f) {
            time_ = options_.loop ? duration : 0.0f;
            return;
        }

        if (time_ <= duration) {
            return;
        }

        if (options_.loop) {
            // 1 フレームで複数周するほど speed が大きい場合も剰余で正しく巻き戻す。
            time_ = std::fmod(time_, duration);
            return;
        }

        time_ = duration;
        state_ = options_.holdAtEnd ? State::Paused : State::Finishing;
    }

    bool CameraSequencePlayer::Evaluate(CameraSnapshot& outSnapshot,
        const CameraSequenceAimContext* aim) const
    {
        if (state_ == State::Stopped || !asset_) {
            return false;
        }

        return CameraSequenceEvaluator::Evaluate(*asset_, time_, outSnapshot, aim);
    }

    void CameraSequencePlayer::Seek(float time)
    {
        time_ = std::clamp(time, 0.0f, GetDuration());

        // 飛び越した区間のイベントは発火させない。エディタでつまみを動かすたびに
        // 揺れが出ると構図の確認ができなくなる。
        eventCursor_ = time_;
    }

    float CameraSequencePlayer::GetDuration() const
    {
        return asset_ ? asset_->timelineLength : 0.0f;
    }

    float CameraSequencePlayer::GetNormalizedTime() const
    {
        const float duration = GetDuration();
        return (duration > 0.0f) ? std::clamp(time_ / duration, 0.0f, 1.0f) : 0.0f;
    }

    float CameraSequencePlayer::GetBlendInWeight() const
    {
        if (options_.blendInSeconds <= 0.0f) {
            return 1.0f;
        }

        return std::clamp(blendInElapsed_ / options_.blendInSeconds, 0.0f, 1.0f);
    }
}
