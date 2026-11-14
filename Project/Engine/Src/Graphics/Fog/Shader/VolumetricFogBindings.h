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
        gVolumetricFogMedia,
        gNoise,
        gVBuffer,
        gEmissive,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gVolumetricFog",      ShaderBindingType::CBV, BindingUsage::Required }, // b0
        { "gVolumetricFogMedia", ShaderBindingType::CBV, BindingUsage::Required }, // b1
        // ノイズを使わないフレームでは差さない（そのときシェーダーは読まない）
        { "gNoise",              ShaderBindingType::SRV, BindingUsage::Conditional }, // t0
        { "gVBuffer",            ShaderBindingType::UAV, BindingUsage::Required }, // u0
        { "gEmissive",           ShaderBindingType::UAV, BindingUsage::Required }, // u1
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::VolumetricFogLightScatteringBind
{
    /// @brief VolumetricFogLightScattering.CS.hlsl の契約
    enum Slot : size_t {
        gVolumetricFog,
        gVolumetricFogLocalLights,
        gVBuffer,
        gSkyIrradianceSH,
        gCloudShadowMap,
        gLightScatteringHistory,
        gSunDepth,
        gEmissive,
        gLightScattering,
        gFarLight,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gVolumetricFog",   ShaderBindingType::CBV, BindingUsage::Required }, // b0
        { "gVolumetricFogLocalLights", ShaderBindingType::CBV, BindingUsage::Required }, // b1
        { "gVBuffer",         ShaderBindingType::SRV, BindingUsage::Required }, // t0
        // 大気が無いシーンでは差さない（そのときシェーダーは読まない）
        { "gSkyIrradianceSH", ShaderBindingType::SRV, BindingUsage::Conditional }, // t1
        // 雲が無いシーンでは差さない（そのときシェーダーは読まない）
        { "gCloudShadowMap",  ShaderBindingType::SRV, BindingUsage::Conditional }, // t2
        { "gLightScatteringHistory", ShaderBindingType::SRV, BindingUsage::Required }, // t3
        // 太陽から見た深さの画像を作らないフレームでは差さない（そのときシェーダーは読まない）
        { "gSunDepth",        ShaderBindingType::SRV, BindingUsage::Conditional }, // t4
        { "gEmissive",        ShaderBindingType::SRV, BindingUsage::Required }, // t5
        { "gLightScattering", ShaderBindingType::UAV, BindingUsage::Required }, // u0
        { "gFarLight",        ShaderBindingType::UAV, BindingUsage::Required }, // u1
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
