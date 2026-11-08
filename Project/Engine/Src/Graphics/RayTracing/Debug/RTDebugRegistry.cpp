#include "pch.h"
#include "RTDebugRegistry.h"

#ifdef CORE_EDITOR

#include <cstring>
#include <iterator>

#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvFreezeScene{ "r.RT.Debug.FreezeScene", false,
            "TLAS と材質の表を作り直さず、止めた時点の物のまま撃つ（物を動かしても影と映り込みが変わらない）",
            {}, CVarFlags::NoSave | CVarFlags::NoUI };

        CVar<bool> cvFixedNoise{ "r.RT.Debug.FixedNoise", false,
            "レイトレーシングの乱数の番号を毎フレーム同じにする（影のノイズが止まる）",
            {}, CVarFlags::NoSave | CVarFlags::NoUI };

        CVar<bool> cvStopAll{ "r.RT.Debug.StopAll", false,
            "レイトレーシングの効果（影・水面の反射と屈折・コースティクス・海底の高さ・海の範囲）を全部止める",
            {}, CVarFlags::NoSave | CVarFlags::NoUI };

        constexpr const char* kViewModeNames[] = {
            "白黒", "疑似カラー", "ヒートマップ", "正負", "法線", "色そのまま", "番号の色", "ビット",
        };
        static_assert(std::size(kViewModeNames) == static_cast<size_t>(RTDebugViewMode::Count),
            "kViewModeNames と RTDebugViewMode の並びがずれている");
    }

    const char* ToDisplayName(RTDebugViewMode mode)
    {
        const size_t index = static_cast<size_t>(mode);
        return index < std::size(kViewModeNames) ? kViewModeNames[index] : "?";
    }

    bool UsesSingleComponent(RTDebugViewMode mode)
    {
        return mode != RTDebugViewMode::Normal && mode != RTDebugViewMode::Color;
    }

    bool RTDebugEffect::IsActive() const
    {
        return !RTDebug::IsStopped() && RTDebugRegistry::Get().IsEffectEnabled(index_);
    }

    RTDebugRegistry& RTDebugRegistry::Get()
    {
        static RTDebugRegistry registry;
        return registry;
    }

    RTDebugEffect RTDebugRegistry::RegisterEffect(const RTDebugEffectDesc& desc)
    {
        effects_.push_back(EffectEntry{ desc, true });
        return RTDebugEffect(static_cast<uint32_t>(effects_.size() - 1));
    }

    void RTDebugRegistry::RegisterChannel(const RTDebugChannelDesc& desc)
    {
        if (!desc.resolve) {
            return;
        }
        channels_.push_back(desc);
    }

    void RTDebugRegistry::RegisterCheck(const RTDebugCheckDesc& desc)
    {
        if (!desc.run) {
            return;
        }
        checks_.push_back(desc);
    }

    void RTDebugRegistry::SetEffectEnabled(uint32_t index, bool enabled)
    {
        if (index < effects_.size()) {
            effects_[index].enabled = enabled;
        }
    }

    bool RTDebugRegistry::IsEffectEnabled(uint32_t index) const
    {
        return index >= effects_.size() || effects_[index].enabled;
    }

    int RTDebugRegistry::FindChannel(const char* effect, const char* name) const
    {
        if (!effect || !name) {
            return -1;
        }
        for (size_t i = 0; i < channels_.size(); ++i) {
            if (std::strcmp(channels_[i].effect, effect) == 0 && std::strcmp(channels_[i].name, name) == 0) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    namespace RTDebug {
        bool IsSceneFrozen() { return cvFreezeScene.Get(); }
        bool IsNoiseFixed() { return cvFixedNoise.Get(); }
        uint32_t NoiseFrame(uint32_t frame) { return cvFixedNoise.Get() ? 0u : frame; }
        bool IsStopped() { return cvStopAll.Get(); }
        void SetSceneFrozen(bool frozen) { cvFreezeScene.Set(frozen); }
        void SetNoiseFixed(bool fixed) { cvFixedNoise.Set(fixed); }
        void SetStopped(bool stopped) { cvStopAll.Set(stopped); }
    }

    bool ResolveFrameBlackboardImage(const RenderContext& context, const char* name, RTDebugSource& out)
    {
        if (!context.frameBlackboard || !name) {
            return false;
        }
        const FrameBlackboardResource* entry = context.frameBlackboard->GetResource(name);
        if (!entry || !entry->isValid || !entry->resource || entry->srvHandle.ptr == 0) {
            return false;
        }
        out.resource = entry->resource;
        out.srv = entry->srvHandle;
        return true;
    }
}

#endif // CORE_EDITOR
