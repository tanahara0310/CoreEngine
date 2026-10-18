#include "pch.h"
#include "SpriteAnimatorComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/GameObject.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cmath>

REFLECT_DEFINE_BEGIN(CoreEngine::SpriteAnimatorComponent, "スプライトアニメ")
    REFLECT_PARTIAL()
    REFLECT_JSON(SaveClipsToJson, LoadClipsFromJson)
    REFLECT_ACCESSOR("defaultClip", "最初のクリップ", GetDefaultClip, SetDefaultClip,
        p.tooltip = "再生を始めたときに流すクリップの名前（空なら流さない）")
    REFLECT_ACCESSOR("speed", "速さ", GetSpeed, SetSpeed,
        p.range = Range(0.0f, 10.0f, 0.01f), p.tooltip = "倍率。1 で各クリップの fps どおり")
    REFLECT_READONLY_ACCESSOR("clip", "流しているクリップ", GetCurrentClipName)
    REFLECT_READONLY_ACCESSOR("isPlaying", "流している", IsPlaying)
    REFLECT_METHOD("Play", "流す", Play)
    REFLECT_METHOD("Stop", "止める", Stop)
REFLECT_DEFINE_END()
REFLECT_REGISTER(CoreEngine::SpriteAnimatorComponent)
COMPONENT_REGISTER(CoreEngine::SpriteAnimatorComponent)

namespace CoreEngine
{
    bool SpriteAnimatorComponent::RequiresComponent(const IComponent& other) const
    {
        return dynamic_cast<const SpriteRendererComponent*>(&other) != nullptr;
    }

    void SpriteAnimatorComponent::Start()
    {
        if (!defaultClip_.empty()) {
            Play(defaultClip_);
        }
    }

    void SpriteAnimatorComponent::Update()
    {
        if (!playing_) {
            return;
        }
        const SpriteAnimationClip* const clip = FindClip(currentClip_);
        if (!clip) {
            playing_ = false;
            return;
        }

        time_ += Time::DeltaTime() * speed_;

        const int frameCount = std::abs(clip->lastFrame - clip->firstFrame) + 1;
        if (clip->fps > 0.0f) {
            const float period = static_cast<float>(frameCount) / clip->fps;
            if (clip->loop) {
                // ループは 1 周の長さで巻き戻し、時間を大きくしない
                time_ = std::fmod(time_, period);
            } else if (time_ >= period) {
                // ループしないクリップは、最後のコマを 1 コマぶん見せてから止める
                playing_ = false;
            }
        }

        if (SpriteRendererComponent* const sprite = GetSprite()) {
            sprite->SetFrame(ResolveFrame(*clip));
        }
    }

    void SpriteAnimatorComponent::Play(const std::string& clipName)
    {
        const SpriteAnimationClip* const clip = FindClip(clipName);
        if (!clip) {
            return;
        }
        if (playing_ && currentClip_ == clipName) {
            return;
        }
        currentClip_ = clipName;
        time_ = 0.0f;
        playing_ = true;
        if (SpriteRendererComponent* const sprite = GetSprite()) {
            sprite->SetFrame(clip->firstFrame);
        }
    }

    void SpriteAnimatorComponent::Stop()
    {
        playing_ = false;
    }

    const SpriteAnimationClip* SpriteAnimatorComponent::FindClip(const std::string& clipName) const
    {
        for (const SpriteAnimationClip& clip : clips_) {
            if (clip.name == clipName) {
                return &clip;
            }
        }
        return nullptr;
    }

    int SpriteAnimatorComponent::ResolveFrame(const SpriteAnimationClip& clip) const
    {
        const int frameCount = std::abs(clip.lastFrame - clip.firstFrame) + 1;
        const int step = (clip.lastFrame >= clip.firstFrame) ? 1 : -1;
        int index = (clip.fps > 0.0f) ? static_cast<int>(std::floor(time_ * clip.fps)) : 0;
        index = clip.loop ? (index % frameCount) : (std::min)(index, frameCount - 1);
        return clip.firstFrame + step * index;
    }

    SpriteRendererComponent* SpriteAnimatorComponent::GetSprite() const
    {
        return Sibling<SpriteRendererComponent>();
    }

    void SpriteAnimatorComponent::SaveClipsToJson(json& parameters) const
    {
        json clips = json::array();
        for (const SpriteAnimationClip& clip : clips_) {
            clips.push_back({
                { "name", clip.name },
                { "firstFrame", clip.firstFrame },
                { "lastFrame", clip.lastFrame },
                { "fps", clip.fps },
                { "loop", clip.loop },
            });
        }
        parameters["clips"] = std::move(clips);
    }

    void SpriteAnimatorComponent::LoadClipsFromJson(const json& parameters)
    {
        clips_.clear();
        const auto it = parameters.find("clips");
        if (it == parameters.end() || !it->is_array()) {
            return;
        }
        for (const json& entry : *it) {
            if (!entry.is_object()) {
                continue;
            }
            SpriteAnimationClip clip;
            clip.name = JsonManager::SafeGet<std::string>(entry, "name", clip.name);
            clip.firstFrame = JsonManager::SafeGet<int>(entry, "firstFrame", clip.firstFrame);
            clip.lastFrame = JsonManager::SafeGet<int>(entry, "lastFrame", clip.lastFrame);
            clip.fps = JsonManager::SafeGet<float>(entry, "fps", clip.fps);
            clip.loop = JsonManager::SafeGet<bool>(entry, "loop", clip.loop);
            clips_.push_back(std::move(clip));
        }
    }
}
