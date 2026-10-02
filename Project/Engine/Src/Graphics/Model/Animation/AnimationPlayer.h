#pragma once

#include <memory>
#include <optional>
#include <string>

#include "IAnimationController.h"
#include "IAnimationControllerFactory.h"
#include "Graphics/Model/Skeleton/Skeleton.h"

namespace CoreEngine
{
    class ModelResource;
    struct Animation;

    /// @brief モデルのアニメーション再生を制御するクラス
    /// @details 再生・リセット・切り替え・ブレンドの責務を Model から分離したもの。
    /// @note スケルトンの所有者はコントローラー。ブレンド中だけ、混ぜた姿勢を本クラスが持つ。
    class AnimationPlayer {
    public:
        /// @brief コンストラクタ
        /// @param resource アニメーション検索元の ModelResource
        /// @param controller 初期アニメーションコントローラー
        /// @param factory 切り替え/ブレンド時のコントローラー生成ファクトリー
        AnimationPlayer(ModelResource* resource,
            std::unique_ptr<IAnimationController> controller,
            std::unique_ptr<IAnimationControllerFactory> factory);

        /// @brief アニメーションを更新
        /// @param deltaTime デルタタイム（秒）
        void Update(float deltaTime);

        /// @brief アニメーションをリセット
        void Reset();

        /// @brief 現在のアニメーション時刻を取得（秒）
        float GetTime() const;

        /// @brief アニメーションが終了したか確認（ループ再生中は常にfalse）
        bool IsFinished() const;

        /// @brief アニメーションブレンドの実行中か
        bool IsBlending() const;

        /// @brief 現在のスケルトンを取得（ブレンド中は混ぜた姿勢）
        /// @return スケルトンへのポインタ（スケルトンアニメーションでない場合は nullptr）
        const Skeleton* GetSkeleton() const;

        /// @brief アニメーションを切り替える
        /// @param animationName 切り替えるアニメーション名
        /// @param loop ループ再生するか
        /// @return 成功したらtrue
        bool Switch(const std::string& animationName, bool loop = true);

        /// @brief アニメーションをブレンドしながら切り替える
        /// @param animationName 切り替えるアニメーション名
        /// @param blendDuration ブレンド時間（秒）
        /// @param loop ループ再生するか
        /// @return 成功したらtrue
        bool SwitchWithBlend(const std::string& animationName, float blendDuration = 0.3f, bool loop = true);

    private:
        /// @brief ブレンド中の状態
        struct Blend {
            std::unique_ptr<IAnimationController> target;  ///< 切り替え先のコントローラー
            float elapsed = 0.0f;                          ///< ブレンドを始めてからの時間（秒）
            float duration = 0.0f;                         ///< ブレンドにかける時間（秒）
            std::optional<Skeleton> pose;                  ///< 混ぜた姿勢（最初の Update までは無い）
        };

        /// @brief 切り替え先アニメーションを検索し、前提条件を検証する（失敗時はログ出力）
        const Animation* FindAnimationForSwitch(const std::string& animationName) const;

        // アニメーション検索元のリソース
        ModelResource* resource_ = nullptr;

        // 再生中のコントローラー（ブレンド中は切り替え元）
        std::unique_ptr<IAnimationController> controller_;

        // ブレンド中の状態（ブレンドしていなければ無い）
        std::optional<Blend> blend_;

        // コントローラー生成ファクトリー
        std::unique_ptr<IAnimationControllerFactory> factory_;
    };
}
