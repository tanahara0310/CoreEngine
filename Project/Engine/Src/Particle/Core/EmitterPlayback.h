#pragma once

#include "Particle/Modules/EmissionModule.h"
#include "Particle/Modules/MainModule.h"

#include <cstdint>

namespace CoreEngine
{
    /// @brief 粒を出す時間軸（再生の状態・周期の中の経過時間・バースト済みの印・放出数の端数）
    /// @details CPU 版と GPU 版のパーティクルが 1 つずつ持ち、1 フレームに出す数をこれで決める。
    ///          周期の長さはメインモジュールの長さ。ループなら越えた分を次の周期へ持ち越し、ループなしなら長さで終わる。
    ///          バーストは周期ごとに 1 回、周期の中の時刻がバーストの時刻（長さより後なら長さ）に達したフレームで出す。
    class EmitterPlayback
    {
    public:
        enum class State {
            Stopped,   ///< 止めている（Play で先頭から）
            Playing,   ///< 再生中
            Finished,  ///< ループなしで長さまで再生した
        };

        /// @brief 先頭から再生する（経過時間・バースト・放出数の端数を戻す）
        void Play();

        /// @brief 放出を止める（生きている粒はそのまま）
        void Stop() { state_ = State::Stopped; }

        /// @brief 時間を進め、このフレームに出す数を返す
        /// @return 放出レートの分とバーストの分の合計（放出モジュールの有効・無効は見ない）
        uint32_t Advance(float deltaTime, const MainModule::MainData& main, const EmissionModule::EmissionData& emission);

        State GetState() const { return state_; }
        bool IsPlaying() const { return state_ == State::Playing; }

        /// @brief 今の周期の中の経過時間（秒）
        float GetElapsedTime() const { return elapsed_; }

        /// @brief 今の周期のバーストを出したか
        bool IsBurstDone() const { return burstDone_; }

    private:
        State state_ = State::Stopped;
        float elapsed_ = 0.0f;
        bool burstDone_ = false;
        float emitRemainder_ = 0.0f;
    };
}
