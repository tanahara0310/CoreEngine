#pragma once

#include "Utility/FrameRate/Time.h"

namespace CoreEngine
{
    /// @brief ゲームの進行状態（Unity / UE の再生・停止に対応する）
    enum class PlaybackState
    {
        Playing, ///< 再生中。ゲームループが通常どおり進む
        Stopped  ///< 停止中。ゲームの更新は進まないが、パラメータは編集できる
    };

    /// @brief 再生 / 停止の唯一の持ち主
    /// @details メニューバー中央の再生・停止ボタンがここを切り替え、
    ///          `BaseScene::Update()` がこれを見てゲームロジックの前進を止める。
    ///
    ///          停止中に「止まるもの / 止まらないもの」は次のとおり。
    ///          - 止まる … シーンの `OnUpdate` / GameObject とコンポーネントの更新 /
    ///                     `OnLateUpdate`、および `Time::DeltaTime()`（0 を返す）
    ///          - 止まらない … 描画・ImGui・エディタカメラ・ギズモ・各パネル・
    ///                         シーン遷移、および `Time::UnscaledDeltaTime()`
    ///
    /// @note Unity の再生終了と違い、停止しても再生前の状態へ巻き戻さない。
    ///       止めた瞬間のシーンをそのまま編集するためのもの。
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
        /// @note ゲーム時間の停止（`Time::SetPaused`）もここで同期させる。
        ///       状態の持ち主を 1 か所にしておかないと、ボタンの見た目と
        ///       実際の進行がすぐに食い違う。
        void SetState(PlaybackState state)
        {
            state_ = state;
            Time::SetPaused(state_ == PlaybackState::Stopped);
        }

        /// @brief 再生中かどうか
        bool IsPlaying() const { return state_ == PlaybackState::Playing; }

        /// @brief 停止中かどうか
        bool IsStopped() const { return state_ == PlaybackState::Stopped; }

        /// @brief 再生を開始
        void Play() { SetState(PlaybackState::Playing); }

        /// @brief 更新を止める
        void Stop() { SetState(PlaybackState::Stopped); }

    private:
        PlaybackStateManager() = default;
        ~PlaybackStateManager() = default;
        PlaybackStateManager(const PlaybackStateManager&) = delete;
        PlaybackStateManager& operator=(const PlaybackStateManager&) = delete;

        /// @brief 初期状態は再生中
        /// @note エディタ UI を持たない Release ビルドには停止を解除する手段が無いため、
        ///       既定を停止にしてはいけない。
        PlaybackState state_ = PlaybackState::Playing;
    };
}
