#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::RTShadowBind
{
    /// @brief RTShadow.hlsl（lib_6_6）が要求するリソースの契約
    /// @note ディスパッチのたびに全部差すのですべて Required
    enum Slot : size_t {
        gShadowOutput,
        gScene,
        gSceneDepth,
        gNormalRoughness,
        gShadowHistory,
        ShadowRayConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gShadowOutput",      ShaderBindingType::UAV, BindingUsage::Required },  // u0
        { "gScene",             ShaderBindingType::SRV, BindingUsage::Required },  // t0: TLAS
        { "gSceneDepth",        ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gNormalRoughness",   ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gShadowHistory",     ShaderBindingType::SRV, BindingUsage::Required },  // t3: 前フレーム履歴（適応サンプリング判定）
        { "ShadowRayConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート CBV）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
