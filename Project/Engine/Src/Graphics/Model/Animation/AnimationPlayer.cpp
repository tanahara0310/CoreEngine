#include "pch.h"
#include "AnimationPlayer.h"

#include "AnimationUtils.h"
#include "Graphics/Model/ModelResource.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

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
        if (!controller_) {
            return;
        }
        controller_->Update(deltaTime);
        if (!blend_) {
            return;
        }

        // 切り替え先も同じ時間だけ進め、経過の割合を切り替え先の重みにして姿勢を混ぜる
        blend_->target->Update(deltaTime);
        blend_->elapsed += deltaTime;
        const float weight = blend_->duration > 0.0f
            ? (std::min)(1.0f, blend_->elapsed / blend_->duration)
            : 1.0f;
        const Skeleton* from = controller_->GetSkeleton();
        const Skeleton* to = blend_->target->GetSkeleton();
        if (from && to) {
            blend_->pose = AnimationUtils::BlendSkeletons(*from, *to, weight);
        }

        // ブレンドし終えたら、切り替え先をそのまま再生中のコントローラーにする
        if (blend_->elapsed >= blend_->duration) {
            controller_ = std::move(blend_->target);
            blend_.reset();
        }
    }

    void AnimationPlayer::Reset() {
        if (controller_) {
            controller_->Reset();
        }
        blend_.reset();
    }

    float AnimationPlayer::GetTime() const {
        return controller_ ? controller_->GetAnimationTime() : 0.0f;
    }

    bool AnimationPlayer::IsFinished() const {
        return controller_ ? controller_->IsFinished() : true;
    }

    bool AnimationPlayer::IsBlending() const {
        return blend_.has_value();
    }

    const Skeleton* AnimationPlayer::GetSkeleton() const {
        if (blend_ && blend_->pose) {
            return &*blend_->pose;
        }
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

        // 今の姿勢から新しいコントローラーを生成し（ファクトリーがコピーを保持する）、ブレンド中ならやめる
        auto next = factory_->CreateSkeletonAnimator(*GetSkeleton(), *newAnimation, loop);
        blend_.reset();
        controller_ = std::move(next);

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
            "Switched to animation: " + animationName);
        return true;
    }

    bool AnimationPlayer::SwitchWithBlend(const std::string& animationName, float blendDuration, bool loop) {
        const Animation* newAnimation = FindAnimationForSwitch(animationName);
        if (!newAnimation) {
            return false;
        }

        // 切り替え先は今の姿勢を初期姿勢として作る。ブレンド中なら切り替え先だけ差し替え、時間を数え直す
        auto target = factory_->CreateSkeletonAnimator(*GetSkeleton(), *newAnimation, loop);
        if (!blend_) {
            blend_.emplace();
        }
        blend_->target = std::move(target);
        blend_->elapsed = 0.0f;
        blend_->duration = blendDuration;

        Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}",
            "Started blend to animation: " + animationName);
        return true;
    }
}
