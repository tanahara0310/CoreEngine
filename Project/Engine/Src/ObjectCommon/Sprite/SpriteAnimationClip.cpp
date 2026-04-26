#include "SpriteAnimationClip.h"

namespace CoreEngine
{
    void SpriteAnimationClip::AddFrame(
        float texLeft, float texTop,
        float texWidth, float texHeight,
        float duration)
    {
        frames_.push_back({ texLeft, texTop, texWidth, texHeight, duration, "" });
    }

    void SpriteAnimationClip::AddFrameWithTexture(
        const std::string& texturePath, float duration)
    {
        // UV はテクスチャ全体（texWidth/texHeight が 0 → ApplyFrame 側で全体扱い）
        frames_.push_back({ 0.0f, 0.0f, 0.0f, 0.0f, duration, texturePath });
    }

    SpriteAnimationClip SpriteAnimationClip::CreateFromTextures(
        const std::vector<std::string>& texturePaths,
        float fps,
        bool  loop)
    {
        SpriteAnimationClip clip;
        clip.SetLoop(loop);

        const float duration = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 60.0f);
        for (const auto& path : texturePaths) {
            clip.AddFrameWithTexture(path, duration);
        }
        return clip;
    }

    SpriteAnimationClip SpriteAnimationClip::CreateUniform(
        float frameWidth, float frameHeight,
        int   frameCount, int   columns,
        float fps,
        bool  loop,
        float startX,
        float startY)
    {
        SpriteAnimationClip clip;
        clip.SetLoop(loop);

        const float duration = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 60.0f);

        for (int i = 0; i < frameCount; ++i) {
            int col = i % columns;
            int row = i / columns;
            float left = startX + col * frameWidth;
            float top = startY + row * frameHeight;
            clip.AddFrame(left, top, frameWidth, frameHeight, duration);
        }

        return clip;
    }

} // namespace CoreEngine
