#include "pch.h"
#include "SpriteAnimator.h"
#include "SpriteObject.h"
#include "Math/Vector/Vector2.h"

namespace CoreEngine
{
    void SpriteAnimator::AddClip(const std::string& name, const SpriteAnimationClip& clip)
    {
        clips_[name] = clip;
    }

    const SpriteAnimationClip* SpriteAnimator::GetClip(const std::string& name) const
    {
        auto it = clips_.find(name);
        return (it != clips_.end()) ? &it->second : nullptr;
    }

    void SpriteAnimator::Play(const std::string& name)
    {
        auto it = clips_.find(name);
        if (it == clips_.end()) {
            return;
        }

        currentClipName_ = name;
        currentClip_ = &it->second;
        currentFrame_ = 0;
        frameTimer_ = 0.0f;
        isPlaying_ = true;
        isPaused_ = false;
    }

    void SpriteAnimator::Stop()
    {
        isPlaying_ = false;
        isPaused_ = false;
        currentFrame_ = 0;
        frameTimer_ = 0.0f;
    }

    void SpriteAnimator::Pause()
    {
        if (isPlaying_) {
            isPaused_ = true;
        }
    }

    void SpriteAnimator::Resume()
    {
        if (isPlaying_) {
            isPaused_ = false;
        }
    }

    void SpriteAnimator::Update(float deltaTime, SpriteObject* sprite)
    {
        if (!isPlaying_ || isPaused_ || !currentClip_ || !sprite) {
            return;
        }

        if (currentClip_->GetFrameCount() == 0) {
            return;
        }

        // 現在フレームの UV を毎フレーム適用（Play 直後の初回表示にも対応）
        ApplyFrame(sprite);

        frameTimer_ += deltaTime;

        const AnimationFrame& frame = currentClip_->GetFrames()[currentFrame_];

        // 現フレームの表示時間を超えたら次へ
        if (frameTimer_ >= frame.duration) {
            frameTimer_ -= frame.duration;
            ++currentFrame_;

            const int frameCount = currentClip_->GetFrameCount();

            if (currentFrame_ >= frameCount) {
                if (currentClip_->IsLoop()) {
                    // ループ：先頭に戻る
                    currentFrame_ = 0;
                } else {
                    // 非ループ：最終フレームで停止
                    currentFrame_ = frameCount - 1;
                    isPlaying_ = false;
                    if (onFinished_) {
                        onFinished_();
                    }
                }
            }

            ApplyFrame(sprite);
        }
    }

    void SpriteAnimator::ApplyFrame(SpriteObject* sprite) const
    {
        if (!currentClip_ || currentFrame_ < 0 ||
            currentFrame_ >= currentClip_->GetFrameCount()) {
            return;
        }

        const AnimationFrame& f = currentClip_->GetFrames()[currentFrame_];

        // フレームにテクスチャパスが指定されていれば差し替える
        if (!f.texturePath.empty()) {
            sprite->SetTexture(f.texturePath);
        }

        // texWidth/texHeight が 0 の場合はテクスチャ全体を使用
        if (f.texWidth <= 0.0f || f.texHeight <= 0.0f) {
            sprite->SetUVRect(0.0f, 0.0f, 1.0f, 1.0f);
            return;
        }

        const Vector2 texSize = sprite->GetTextureSize();
        if (texSize.x <= 0.0f || texSize.y <= 0.0f) { return; }

        // ピクセル座標を正規化 UV に変換して適用
        const float uLeft = f.texLeft / texSize.x;
        const float uRight = (f.texLeft + f.texWidth) / texSize.x;
        const float vTop = f.texTop / texSize.y;
        const float vBottom = (f.texTop + f.texHeight) / texSize.y;

        sprite->SetUVRect(uLeft, vTop, uRight, vBottom);
    }

} // namespace CoreEngine
