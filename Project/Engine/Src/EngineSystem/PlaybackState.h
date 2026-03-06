#pragma once

namespace CoreEngine
{
    /// @brief ゲームの再生状態を管理する列挙型
    enum class PlaybackState
    {
        Playing,  // 再生中
        Paused    // 一時停止中
    };

    /// @brief ゲームの再生状態を管理するクラス
    class PlaybackStateManager
    {
    public:
        /// @brief 唯一のインスタンスを取得
        static PlaybackStateManager& GetInstance()
        {
            static PlaybackStateManager instance;
            return instance;
        }

        /// @brief 現在の再生状態を取得
        PlaybackState GetState() const { return state_; }

        /// @brief 再生状態を設定
        void SetState(PlaybackState state) { state_ = state; }

        /// @brief 再生中かどうか
        bool IsPlaying() const { return state_ == PlaybackState::Playing; }

        /// @brief 一時停止中かどうか
        bool IsPaused() const { return state_ == PlaybackState::Paused; }

        /// @brief 再生を開始
        void Play() { state_ = PlaybackState::Playing; }

        /// @brief 一時停止
        void Pause() { state_ = PlaybackState::Paused; }

    private:
        PlaybackStateManager() = default;
        ~PlaybackStateManager() = default;
        PlaybackStateManager(const PlaybackStateManager&) = delete;
        PlaybackStateManager& operator=(const PlaybackStateManager&) = delete;

        PlaybackState state_ = PlaybackState::Paused; // 初期はポーズ状態
    };
}
