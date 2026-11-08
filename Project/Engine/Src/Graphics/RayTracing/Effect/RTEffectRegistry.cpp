#include "pch.h"
#include "RTEffectRegistry.h"

#include <algorithm>
#include <cstring>

#include "Graphics/Render/Pass/RenderPipeline.h"

namespace CoreEngine
{
    RTEffectRegistry& RTEffectRegistry::Get()
    {
        static RTEffectRegistry registry;
        return registry;
    }

    bool RTEffectRegistry::Register(RTEffectDesc desc)
    {
        effects_.push_back(std::move(desc));
        sorted_ = false;
        return true;
    }

    const std::vector<RTEffectDesc>& RTEffectRegistry::GetEffects()
    {
        // 静的初期化の順は翻訳単位ごとに決まらないので、名前の順に並べ直す
        if (!sorted_) {
            std::stable_sort(effects_.begin(), effects_.end(),
                [](const RTEffectDesc& a, const RTEffectDesc& b) {
                    return std::strcmp(a.id ? a.id : "", b.id ? b.id : "") < 0;
                });
            sorted_ = true;
        }
        return effects_;
    }

    void RTEffectRegistry::AddPasses(RenderPipeline& pipeline)
    {
        for (const RTEffectDesc& effect : GetEffects()) {
            for (const RTEffectPassDesc& pass : effect.passes) {
                if (pass.create) {
                    pipeline.AddPass(pass.create(), pass.phase, pass.priority, pass.timingCategory);
                }
            }
        }
    }
}
