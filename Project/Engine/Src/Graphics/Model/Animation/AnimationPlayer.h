#pragma once

#include <memory>
#include <string>

#include "IAnimationController.h"
#include "IAnimationControllerFactory.h"

namespace CoreEngine
{
    class ModelResource;
    struct Skeleton;
    struct Animation;

    /// @brief モデルのアニメーション再生を制御するクラス
    /// @details 再生・リセット・切り替え・ブレンドの責務を Model から分離したもの。
    /// @note スケルトンの所有者はコントローラーであり、本クラスも Model もコピーを持たない。
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

        /// @brief 現在のスケルトンを取得（コントローラーが所有する実体への参照）
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
        /// @brief 切り替え先アニメーションを検索し、前提条件を検証する（失敗時はログ出力）
        const Animation* FindAnimationForSwitch(const std::string& animationName) const;

        // アニメーション検索元のリソース
        ModelResource* resource_ = nullptr;

        // 再生中のコントローラー（SkeletonAnimator / AnimationBlender）
        std::unique_ptr<IAnimationController> controller_;

        // コントローラー生成ファクトリー
        std::unique_ptr<IAnimationControllerFactory> factory_;
    };
}
