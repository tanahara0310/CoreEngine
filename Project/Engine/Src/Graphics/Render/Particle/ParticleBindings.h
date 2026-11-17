#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief パーティクルの描画シェーダーが要求するリソースの契約
/// @details 添字は enum Slot、実体は BaseParticleRenderer が持つ BindingTable（描画中に名前で引かない）。

namespace CoreEngine::ParticleBind
{
    /// @brief Particle.VS/PS.hlsl と ModelParticle.VS/PS.hlsl の契約
    enum Slot : size_t {
        gParticle,
        gTexture,
        gFog,
        gVolumetricFogIntegrated,
        gVolumetricFogFarLight,
        gVolumetricFogFarMedia,
        gLightCounts,
        gDirectionalLights,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gParticle", ShaderBindingType::SRV, BindingUsage::Required },
        { "gTexture",  ShaderBindingType::SRV, BindingUsage::Required },
        // FogPass がフォグ定数を供給したフレームだけ差す
        { "gFog",      ShaderBindingType::CBV, BindingUsage::Conditional },
        // フォグの定数が格子を使うビューだけ差す
        { "gVolumetricFogIntegrated", ShaderBindingType::SRV, BindingUsage::Conditional }, // t0, space6
        { "gVolumetricFogFarLight",   ShaderBindingType::SRV, BindingUsage::Conditional }, // t1, space6
        { "gVolumetricFogFarMedia",   ShaderBindingType::CBV, BindingUsage::Conditional }, // b0, space6
        // ModelParticle.PS.hlsl だけが持つ。ライトマネージャーがあるときに ModelParticleRenderer が差す
        { "gLightCounts",       ShaderBindingType::CBV, BindingUsage::Optional },
        { "gDirectionalLights", ShaderBindingType::SRV, BindingUsage::Optional },
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
