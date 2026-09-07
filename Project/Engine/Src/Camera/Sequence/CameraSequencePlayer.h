#pragma once

#include "Camera/Sequence/CameraSequenceEvaluator.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// @file
/// @brief カメラシーケンスの再生ランタイム（再生ヘッドの進行と状態管理）

namespace CoreEngine
{
    /// @brief 再生の指定
    struct CameraSequencePlaybackOptions {
        /// @brief 終端で先頭へ戻すか
        bool loop = false;

        /// @brief 再生速度（1.0 が等倍）
        float speed = 1.0f;

        /// @brief 再生開始時、直前のカメラ姿勢から繋ぐ秒数（0 でカット）
        /// @note 繋ぎ元は「再生を始めた瞬間のカメラ」なので、実際に混ぜるのは
        ///       カメラを持っている CameraSequenceFeature の仕事。
        float blendInSeconds = 0.0f;

        /// @brief ポーズ・スローの影響を受けない時間で進めるか
        /// @details 演出カットやポーズメニューの背景カメラは true にする。
        bool useUnscaledTime = false;

        /// @brief 終端に達した後もカメラを握り続けるか
        /// @details 既定は false で、最後のキーの構図を 1 フレーム出してから停止し、
        ///          カメラをゲーム側へ返す。true にすると最後の構図で止まったままになるので、
        ///          明示的に CameraSequence::Stop() を呼ぶまでゲーム側の追従は戻らない。
        bool holdAtEnd = false;
    };

    /// @brief シーケンスを 1 本再生する状態機械
    ///
    /// @details
    /// **Camera にもエンジンにも依存しない**のがこのクラスの要点。入力は経過時間だけ、
    /// 出力は CameraSnapshot で、カメラへ実際に書き込むのは CameraSequenceFeature の仕事。
    /// CameraShaker と同じ役割分担で、単体でテストできる。
    ///
    /// @note メインスレッド専用。
    class CameraSequencePlayer {
    public:
        /// @brief シーケンスを頭から再生する
        /// @param asset 再生するシーケンス（nullptr / キー無しなら停止扱い）
        /// @param options 再生の指定
        /// @param name 表示用の名前（エディタの「今なにが動いているか」表示に使う）
        void Play(std::shared_ptr<const CameraSequenceAsset> asset,
            const CameraSequencePlaybackOptions& options = {},
            const std::string& name = {});

        /// @brief 再生を止めて先頭へ戻す
        void Stop();

        /// @brief 再生ヘッドを止める（姿勢はその位置で保持される）
        void Pause();

        /// @brief 一時停止から再開する
        void Resume();

        /// @brief 再生ヘッドを進める
        /// @param scaledDeltaTime Time::DeltaTime()
        /// @param unscaledDeltaTime Time::UnscaledDeltaTime()
        void Update(float scaledDeltaTime, float unscaledDeltaTime);

        /// @brief 現在の再生ヘッド位置のカメラ姿勢を求める
        /// @param aim 注視対象の解決口（nullptr 可）
        /// @return 停止中・シーケンス未設定なら false
        bool Evaluate(CameraSnapshot& outSnapshot,
            const CameraSequenceAimContext* aim = nullptr) const;

        /// @brief 再生ヘッドを直接動かす（スクラブ用）
        /// @note 跨いだイベントは発火しない。エディタでつまみを動かすたびに
        ///       揺れが出ると、構図の確認ができなくなる。
        void Seek(float time);

        /// @brief 直前の Update で跨いだイベント（時刻の昇順）
        /// @details Update のたびに作り直される。呼び出し側はこれを見て実際の演出を起こす。
        ///          Player 自身はシェイクもイベントバスも知らない。
        const std::vector<CameraSequenceEvent>& GetFiredEvents() const { return firedEvents_; }

        // ===== 状態の照会 =====

        /// @brief 再生ヘッドが進んでいるか
        bool IsPlaying() const { return state_ == State::Playing; }

        /// @brief 一時停止中か
        bool IsPaused() const { return state_ == State::Paused; }

        /// @brief シーケンスがカメラを握っているか（一時停止中も含む）
        /// @details カメラへ書き込むかどうかの判断はこちらを見ること。
        bool IsActive() const { return state_ != State::Stopped; }

        /// @brief 終端に達して停止待ちか
        bool IsFinishing() const { return state_ == State::Finishing; }

        /// @brief 再生中のシーケンス名（停止中は空）
        const std::string& GetName() const { return name_; }

        /// @brief 現在の再生ヘッド位置 [秒]
        float GetTime() const { return time_; }

        /// @brief シーケンス全体の長さ [秒]（未設定なら 0）
        float GetDuration() const;

        /// @brief 再生位置を 0..1 で取得
        float GetNormalizedTime() const;

        /// @brief 再生の指定
        const CameraSequencePlaybackOptions& GetOptions() const { return options_; }

        /// @brief ブレンドインの進み具合（0..1、1 で繋ぎ終わり）
        float GetBlendInWeight() const;

        /// @brief Play() のたびに増える通し番号
        /// @details 「同じシーケンスが再度頭から再生された」ことを外から検出するための印。
        ///          再生中にもう一度 Play() されても値が変わるので、IsActive() の変化だけを
        ///          見ていると取りこぼす繋ぎ直しを拾える。停止中は 0。
        std::uint32_t GetPlayId() const { return playId_; }

    private:
        /// @brief 再生ヘッドを終端で折り返す / 止める
        void AdvanceTime(float delta);

        /// @brief 半開区間 (fromTime, toTime] に入るイベントを firedEvents_ へ積む
        /// @details 端点の扱いを半開にしないと、ループのたびに時刻 0 のイベントが
        ///          二重に出たり、逆に一度も出なかったりする。
        void CollectEvents(float fromTime, float toTime);

        enum class State {
            Stopped,
            Playing,
            Paused,
            /// @brief 終端に達し、最後の構図を 1 フレーム出してから停止する途中
            /// @details ここで即 Stopped にすると最後のキーの構図が 1 度も描画されない。
            Finishing
        };

        std::shared_ptr<const CameraSequenceAsset> asset_;
        CameraSequencePlaybackOptions options_{};
        std::string name_;

        State state_ = State::Stopped;
        float time_ = 0.0f;
        float blendInElapsed_ = 0.0f;
        std::uint32_t playId_ = 0;
        std::uint32_t nextPlayId_ = 1;

        // 直前の Update で跨いだイベント
        std::vector<CameraSequenceEvent> firedEvents_;

        // 次に拾う範囲の下限（この時刻より後のイベントが対象）。
        // 再生開始時は -1 にして、時刻 0 ちょうどのイベントも拾えるようにする。
        // Seek はここを移動先へ合わせるだけなので、飛び越した分は発火しない。
        float eventCursor_ = -1.0f;
    };
}
