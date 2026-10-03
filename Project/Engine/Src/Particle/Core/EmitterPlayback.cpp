#include "pch.h"
#include "EmitterPlayback.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace
    {
        /// 周期の長さの下限（0 で割らないため）
        constexpr float kMinDuration = 1.0e-3f;
    }

    void EmitterPlayback::Play()
    {
        state_ = State::Playing;
        elapsed_ = 0.0f;
        burstDone_ = false;
        emitRemainder_ = 0.0f;
    }

    uint32_t EmitterPlayback::Advance(float deltaTime, const MainModule::MainData& main,
                                      const EmissionModule::EmissionData& emission)
    {
        if (state_ != State::Playing || deltaTime <= 0.0f) {
            return 0;
        }

        const float duration = (std::max)(main.duration, kMinDuration);
        const float burstTime = std::clamp(emission.burstTime, 0.0f, duration);

        float time = elapsed_ + deltaTime;
        float emittingTime = deltaTime;
        uint32_t bursts = 0;

        // 今の周期のバースト
        if (!burstDone_ && time >= burstTime) {
            burstDone_ = true;
            ++bursts;
        }

        if (time >= duration) {
            if (main.looping) {
                // 越えた分は次の周期へ持ち越す。途中で丸ごと過ぎた周期のバーストも出す
                const float remainder = std::fmod(time, duration);
                const long wraps = (std::max)(std::lround((time - remainder) / duration), 1L);
                bursts += static_cast<uint32_t>(wraps - 1);
                burstDone_ = remainder >= burstTime;
                if (burstDone_) {
                    ++bursts;
                }
                time = remainder;
            } else {
                // 長さまでの分だけ出して終わる
                emittingTime = (std::max)(duration - elapsed_, 0.0f);
                time = duration;
                state_ = State::Finished;
            }
        }

        emitRemainder_ += static_cast<float>(emission.rateOverTime) * emittingTime;
        const uint32_t rateCount = static_cast<uint32_t>(emitRemainder_);
        emitRemainder_ -= static_cast<float>(rateCount);

        elapsed_ = time;
        return rateCount + bursts * emission.burstCount;
    }
}
