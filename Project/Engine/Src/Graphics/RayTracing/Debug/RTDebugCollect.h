#pragma once

#include <cstdint>
#include <vector>

#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RayTracing/Effect/RTEffectSet.h"
#include "Graphics/Render/RenderDomainContext.h"

/// @file
/// @brief 効果の申告（RTDebugEffectDesc::collect）に渡す、よく使う集め方

namespace CoreEngine
{
    /// @brief CollectGameViewDispatch が行を足す条件
    enum class RTDebugGameViewRow : uint8_t {
        Always,          ///< 効果が作られていれば足す
        OnlyDispatched,  ///< 効果が初期化済みで、直近のディスパッチの記録がある（status が None でない）ときだけ足す
    };

    /// @brief 効果の直近のディスパッチを「ゲームの画面」の 1 行として集める（RTDebugEffectDesc::collect に渡す）
    /// @tparam TEffect RTEffectSet から型で引く効果（GetDispatchInfo() を持つ）
    /// @tparam kRule 行を足す条件
    template <class TEffect, RTDebugGameViewRow kRule = RTDebugGameViewRow::Always>
    void CollectGameViewDispatch(const RTDebugQuery& query, std::vector<RTDebugEffectRow>& rows)
    {
        TEffect* effect = query.domain ? query.domain->GetRTEffects().template Find<TEffect>() : nullptr;
        if (!effect) {
            return;
        }
        if constexpr (kRule == RTDebugGameViewRow::OnlyDispatched) {
            if (!effect->IsInitialized() || effect->GetDispatchInfo().status == RayTracingDispatchStatus::None) {
                return;
            }
        }
        rows.push_back({ "ゲームの画面", effect->GetDispatchInfo() });
    }
}
