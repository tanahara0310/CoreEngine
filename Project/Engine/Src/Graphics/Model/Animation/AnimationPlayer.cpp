#include "pch.h"
#include "AnimationPlayer.h"

#include "Graphics/Model/ModelResource.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    AnimationPlayer::AnimationPlayer(ModelResource* resource,
        std::unique_ptr<IAnimationController> controller,
        std::unique_ptr<IAnimationControllerFactory> factory)
        : resource_(resource)
        , controller_(std::move(controller))
        , factory_(std::move(factory))
    {
    }

    void AnimationPlayer::Update(float deltaTime) {
        if (controller_) {
            controller_->Update(deltaTime);
        }
    }

    void AnimationPlayer::Reset() {
        if (controller_) {
            controller_->Reset();
        }
    }

    float AnimationPlayer::GetTime() const {
        return controller_ ? controller_->GetAnimationTime() : 0.0f;
    }

    bool AnimationPlayer::IsFinished() const {
        return controller_ ? controller_->IsFinished() : true;
    }

    bool AnimationPlayer::IsBlending() const {
        return controller_ ? controller_->IsBlending() : false;
    }

    const Skeleton* AnimationPlayer::GetSkeleton() const {
        return controller_ ? controller_->GetSkeleton() : nullptr;
    }

    const Animation* AnimationPlayer::FindAnimationForSwitch(const std::string& animationName) const {
        if (!resource_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}",
                "Cannot switch animation: ModelResource is null");
            return nullptr;
        }

        const Animation* animation = resource_->GetAnimation(animationName);
        if (!animation) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}",
                "Animation not found: " + animationName);
            return nullptr;
        }

        if (!controller_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}",
                "Cannot switch animation: no animation controller");
            return nullptr;
        }

        if (!factory_) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}",
                "AnimationControllerFactory is not set. Use ModelManager::CreateSkeletonModel()");
            return nullptr;
        }

        // スケルトンを持つコントローラーのみ切り替え/ブレンドが可能
        if (!controller_->GetSkeleton()) {
            Logger::GetInstance().Logf(LogLevel::WARNING, LogCategory::Graphics, "{}",
                "Animation switching is only supported for SkeletonAnimator");
            return nullptr;
        }

        return animation;
    }

    bool AnimationPlayer::Switch(const std::string& animationName, bool loop) {
        const Animation* newAnimation = FindAnimationForSwitch(animationName);
        if (!newAnimation) {
            return false;
        }

        // 現在のスケルトン状態から新しいコントローラーを生成（ファクトリーがコピーを保持する）
        controller_ = factory_->CreateSkeletonAnimator(*controller_->GetSkeleton(), *newAnimation, loop);

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
            "Switched to animation: " + animationName);
        return true;
    }

    bool AnimationPlayer::SwitchWithBlend(const std::string& animationName, float blendDuration, bool loop) {
        const Animation* newAnimation = FindAnimationForSwitch(animationName);
        if (!newAnimation) {
            return false;
        }

        // ブレンド先コントローラーを生成（現在のスケルトン状態を初期姿勢として引き継ぐ）
        auto newAnimator = factory_->CreateSkeletonAnimator(*controller_->GetSkeleton(), *newAnimation, loop);

        if (controller_->IsBlending()) {
            // 既に AnimationBlender として動作中 → ターゲットだけ切り替える（dynamic_cast 不要）
            controller_->AddBlendTarget(std::move(newAnimator), blendDuration);
        } else {
            // SkeletonAnimator からブレンダーへ置き換え
            controller_ = factory_->CreateBlenderWithTarget(
                std::move(controller_), std::move(newAnimator), blendDuration);
        }

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
            "Started blend to animation: " + animationName);
        return true;
    }
}
