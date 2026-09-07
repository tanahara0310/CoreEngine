#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief フォグ合成 CS が要求するリソースの契約
/// @details 添字は enum Slot、実体は BindingTable が持つ（描画中に名前で引かない）。

namespace CoreEngine::FogApplyBind
{
    /// @brief HeightFog.CS.hlsl の契約
    enum Slot : size_t {
        gFog,
        gSceneDepth,
        gSkyViewLUT,
        gOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gFog",        ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gSceneDepth", ShaderBindingType::SRV, BindingUsage::Required },     // t0
        // 空色ブレンドが有効なフレームだけ差す（大気非対応シーンでは未バインド）
        { "gSkyViewLUT", ShaderBindingType::SRV, BindingUsage::Conditional }, // t1
        { "gOutput",     ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
