#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief 空の描画シェーダーが要求するリソースの契約
/// @details 添字は enum Slot、実体は SkyBoxRenderer が持つ BindingTable（描画中に名前で引かない）。

namespace CoreEngine::SkyBoxBind
{
    /// @brief Skybox.VS.hlsl / SkyAtmosphere.PS.hlsl の契約
    enum Slot : size_t {
        gTransformationMatrix,
        gAtmosphere,
        gSkyViewLUT,
        gTransmittanceLUT,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gTransformationMatrix", ShaderBindingType::CBV, BindingUsage::Required },
        { "gAtmosphere",           ShaderBindingType::CBV, BindingUsage::Required },
        { "gSkyViewLUT",           ShaderBindingType::SRV, BindingUsage::Required },
        { "gTransmittanceLUT",     ShaderBindingType::SRV, BindingUsage::Required },
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
