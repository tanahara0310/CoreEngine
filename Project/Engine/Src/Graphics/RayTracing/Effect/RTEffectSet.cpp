#include "pch.h"
#include "RTEffectSet.h"

#include <algorithm>
#include <string_view>

#include "Graphics/RayTracing/Effect/RTEffectRegistry.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    RTEffectSet::~RTEffectSet()
    {
        Shutdown();
    }

    void RTEffectSet::Initialize(const RTEffectInitContext& init, bool rayTracingSupported)
    {
        init_ = init;
        rayTracingSupported_ = rayTracingSupported;
    }

    void RTEffectSet::EnsureGroup(const char* group)
    {
        const std::string_view groupName = group ? group : "";
        if (std::find(createdGroups_.begin(), createdGroups_.end(), groupName) != createdGroups_.end()) {
            return;
        }
        createdGroups_.emplace_back(groupName);

        for (const RTEffectDesc& desc : RTEffectRegistry::Get().GetEffects()) {
            const std::string_view descGroup = desc.group ? desc.group : "";
            if (descGroup != groupName || !desc.create) {
                continue;
            }
            std::unique_ptr<RTEffect> effect = desc.create();
            if (rayTracingSupported_ && effect->Initialize(init_)) {
                Logger::GetInstance().Infof(LogCategory::Graphics,
                    "RTEffectSet: {} を初期化しました", desc.id ? desc.id : "");
            }
            effects_.push_back({ desc.typeKey, desc.id, std::move(effect) });
        }
    }

    void RTEffectSet::WaitForPipelines()
    {
        for (Entry& entry : effects_) {
            entry.effect->WaitForPipelines();
        }
    }

    void RTEffectSet::BeginFrame()
    {
        for (Entry& entry : effects_) {
            entry.effect->BeginFrame();
        }
    }

    void RTEffectSet::OnResize(uint32_t width, uint32_t height)
    {
        for (Entry& entry : effects_) {
            entry.effect->OnResize(width, height);
        }
    }

    void RTEffectSet::Shutdown()
    {
        while (!effects_.empty()) {
            effects_.pop_back();
        }
        createdGroups_.clear();
    }

    RTEffect* RTEffectSet::FindByKey(const void* typeKey) const
    {
        for (const Entry& entry : effects_) {
            if (entry.typeKey == typeKey) {
                return entry.effect.get();
            }
        }
        return nullptr;
    }
}
