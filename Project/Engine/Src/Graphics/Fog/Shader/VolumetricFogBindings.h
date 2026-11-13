#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief ボリュメトリックフォグの格子を作る 3 本の CS が要求するリソースの契約

namespace CoreEngine::VolumetricFogInjectBind
{
    /// @brief VolumetricFogInject.CS.hlsl の契約
    enum Slot : size_t {
        gVolumetricFog,
        gVBuffer,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gVolumetricFog", ShaderBindingType::CBV, BindingUsage::Required }, // b0
        { "gVBuffer",       ShaderBindingType::UAV, BindingUsage::Required }, // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::VolumetricFogLightScatteringBind
{
    /// @brief VolumetricFogLightScattering.CS.hlsl の契約
    enum Slot : size_t {
        gVolumetricFog,
        gVBuffer,
        gSkyIrradianceSH,
        gCloudShadowMap,
        gLightScatteringHistory,
        gSunDepth,
        gLightScattering,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gVolumetricFog",   ShaderBindingType::CBV, BindingUsage::Required }, // b0
        { "gVBuffer",         ShaderBindingType::SRV, BindingUsage::Required }, // t0
        // 大気が無いシーンでは差さない（そのときシェーダーは読まない）
        { "gSkyIrradianceSH", ShaderBindingType::SRV, BindingUsage::Conditional }, // t1
        // 雲が無いシーンでは差さない（そのときシェーダーは読まない）
        { "gCloudShadowMap",  ShaderBindingType::SRV, BindingUsage::Conditional }, // t2
        { "gLightScatteringHistory", ShaderBindingType::SRV, BindingUsage::Required }, // t3
        // 太陽から見た深さの画像を作らないフレームでは差さない（そのときシェーダーは読まない）
        { "gSunDepth",        ShaderBindingType::SRV, BindingUsage::Conditional }, // t4
        { "gLightScattering", ShaderBindingType::UAV, BindingUsage::Required }, // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::VolumetricFogIntegrateBind
{
    /// @brief VolumetricFogIntegrate.CS.hlsl の契約
    enum Slot : size_t {
        gVolumetricFog,
        gLightScattering,
        gIntegrated,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gVolumetricFog",   ShaderBindingType::CBV, BindingUsage::Required }, // b0
        { "gLightScattering", ShaderBindingType::SRV, BindingUsage::Required }, // t0
        { "gIntegrated",      ShaderBindingType::UAV, BindingUsage::Required }, // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
