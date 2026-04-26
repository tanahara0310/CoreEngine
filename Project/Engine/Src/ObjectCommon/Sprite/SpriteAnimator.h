#pragma once

#include "SpriteAnimationClip.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace CoreEngine
{
    class SpriteObject;

    /// @brief スプライトアニメーションの再生を管理するクラス
    /// @details SpriteObject に持たせ、Update() を毎フレーム呼ぶことで
    ///          フレームを自動進行し SetUVRect でテクスチャ範囲を切り替える。
    class SpriteAnimator
    {
    public:
        // ===== クリップ管理 =====

        /// @brief アニメーションクリップを登録
        /// @param name  クリップ識別名
        /// @param clip  登録するクリップ
        void AddClip(const std::string& name, const SpriteAnimationClip& clip);

        /// @brief 登録済みクリップを取得（存在しない場合は nullptr）
        const SpriteAnimationClip* GetClip(const std::string& name) const;

        // ===== 再生制御 =====

        /// @brief アニメーションを再生する
        /// @param name 再生するクリップ名
        /// @note  同じクリップが既に再生中の場合は先頭から再スタートする
        void Play(const std::string& name);

        /// @brief アニメーションを停止し、フレームを先頭に戻す
        void Stop();

        /// @brief アニメーションを一時停止する
        void Pause();

        /// @brief 一時停止を解除して再開する
        void Resume();

        // ===== 毎フレーム更新 =====

        /// @brief フレームを進め、必要であれば SpriteObject の UV を更新する
        /// @param deltaTime 前フレームからの経過時間（秒）
        /// @param sprite    UV を適用する対象の SpriteObject
        void Update(float deltaTime, SpriteObject* sprite);

        // ===== コールバック =====

        /// @brief ループなしアニメーションが最終フレームを終えたときに呼ばれるコールバックを設定
        /// @param callback 終了時に呼ぶ関数
        void SetOnFinished(std::function<void()> callback) { onFinished_ = std::move(callback); }

        // ===== 状態取得 =====

        /// @brief 現在再生中のクリップ名を返す（未再生なら空文字列）
        const std::string& GetCurrentClipName()  const { return currentClipName_; }

        /// @brief 現在のフレームインデックスを返す
        int  GetCurrentFrameIndex() const { return currentFrame_; }

        /// @brief 再生中かどうか（一時停止中は false）
        bool IsPlaying() const { return isPlaying_ && !isPaused_; }

        /// @brief 一時停止中かどうか
        bool IsPaused()  const { return isPaused_; }

    private:
        /// @brief 現在フレームの UV 矩形を sprite に適用する
        void ApplyFrame(SpriteObject* sprite) const;

        // ── データ ──────────────────────────────────────────────
        std::unordered_map<std::string, SpriteAnimationClip> clips_;

        std::string                currentClipName_;
        const SpriteAnimationClip* currentClip_ = nullptr;

        int   currentFrame_ = 0;
        float frameTimer_ = 0.0f;  ///< 現フレームの経過時間（秒）

        bool isPlaying_ = false;
        bool isPaused_ = false;

        std::function<void()> onFinished_;
    };

} // namespace CoreEngine
