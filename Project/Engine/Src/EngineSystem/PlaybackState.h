#pragma once

#include <functional>

namespace CoreEngine
{
    /// @brief ゲームの進行状態（Unity の Play モードに対応する）
    enum class PlaybackState
    {
        Editing, ///< 編集中。ゲームの更新は進まない
        Playing, ///< 再生中。ゲームループが通常どおり進む
        Paused,  ///< 再生の一時停止中。ゲームの更新は進まず、シーンは再生中の状態のまま残る
    };

    /// @brief 再生・一時停止・停止の唯一の持ち主
    /// @details ツールバーの再生ボタンがここを切り替え、`Scene::Update()` が
    ///          `IsAdvancing()` を見てゲームロジックの前進を止める。
    ///
    ///          更新が止まっている間（編集中・一時停止中）に「止まるもの / 止まらないもの」は次のとおり。
    ///          - 止まる … シーンの `OnUpdate` / GameObject とコンポーネントの更新 /
    ///                     `OnLateUpdate`、および `Time::DeltaTime()`（0 を返す）
    ///          - 止まらない … 描画・ImGui・エディタカメラ・ギズモ・各パネル・
    ///                         シーン遷移、および `Time::UnscaledDeltaTime()`
    ///
    ///          再生の開始と停止は頼まれた次のフレームの先頭で行い、`SetTransitionHooks()` の処理を呼ぶ。
    class PlaybackStateManager
    {
    public:
        /// @brief 再生の開始と停止のときに呼ぶ処理
        struct TransitionHooks
        {
            /// 再生を始める直前に呼ぶ。false を返すと、次のフレームでもう一度試す
            std::function<bool()> beforePlay;

            /// 編集中へ戻した直後に呼ぶ。false を返すと再生モードのまま、次のフレームでもう一度試す
            /// @note false を返すときは何も変えないこと。
            std::function<bool()> afterStop;
        };

        /// @brief 唯一のインスタンスを取得
        static PlaybackStateManager& GetInstance();

        /// @brief 現在の状態
        PlaybackState GetState() const;

        /// @brief 再生モード（再生中か一時停止中）か
        bool IsInPlayMode() const { return inPlayMode_; }

        /// @brief 再生中で、一時停止していないか
        bool IsPlaying() const { return GetState() == PlaybackState::Playing; }

        /// @brief 再生モードのまま一時停止しているか
        bool IsPaused() const { return GetState() == PlaybackState::Paused; }

        /// @brief 編集中か
        bool IsEditing() const { return !inPlayMode_; }

        /// @brief このフレームでゲームの更新を進めるか
        bool IsAdvancing() const { return IsPlaying(); }

        /// @brief 一時停止の切り替えが入っているか
        /// @note 編集中に入れておくと、再生を一時停止した状態で始める。
        bool IsPauseToggled() const { return pauseToggled_; }

        /// @brief 再生を始める（編集中なら次のフレームの先頭で始める）
        void Play();

        /// @brief 再生をやめて編集中へ戻す（次のフレームの先頭で戻す）
        void Stop();

        /// @brief 一時停止を切り替える
        void TogglePause();

        /// @brief 一時停止する
        void Pause();

        /// @brief 一時停止したまま 1 フレームだけ進めるよう頼む
        /// @note 再生中に頼むと一時停止してから進める。編集中は何もしない。
        void RequestStep();

        /// @brief 再生の開始と停止のときに呼ぶ処理を設定する
        void SetTransitionHooks(TransitionHooks hooks);

        /// @brief 再生の開始と停止のときに呼ぶ処理を外す
        void ClearTransitionHooks();

        /// @brief 頼まれた再生の開始・停止・コマ送りを取り込む（フレームの先頭で呼ぶ）
        void BeginFrame();

        /// @brief コマ送りの 1 フレームを閉じる（フレームの末尾で呼ぶ）
        void EndFrame();

    private:
        PlaybackStateManager();
        ~PlaybackStateManager() = default;
        PlaybackStateManager(const PlaybackStateManager&) = delete;
        PlaybackStateManager& operator=(const PlaybackStateManager&) = delete;

        /// @brief ゲーム時間の停止を今の状態へ合わせる
        void SyncTime() const;

        /// 再生モードか
        bool inPlayMode_ = false;

        /// 一時停止の切り替え
        bool pauseToggled_ = false;

        /// 再生の開始・停止の頼み（次のフレームの先頭で取り込む）
        bool playRequested_ = false;
        bool stopRequested_ = false;

        /// コマ送りの要求（次のフレームの先頭で取り込む）
        bool stepRequested_ = false;

        /// コマ送りの 1 フレームを進めている最中か
        bool stepping_ = false;

        TransitionHooks hooks_;
    };
}
