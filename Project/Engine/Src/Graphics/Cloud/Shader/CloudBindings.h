#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief 雲の各コンピュートシェーダーが要求するリソースの契約
/// @details どのパスもディスパッチのたびに宣言した全リソースを差すので、すべて Required。
///          添字は enum Slot、実体は BindingTable が持つ（描画中に名前で引かない）。

namespace CoreEngine::CloudNoiseBind
{
    /// @brief ノイズ生成 CS 3 本（BaseShape / Detail / WeatherMap）の共通契約
    /// @note 純手続き生成なので定数バッファを持たない
    enum Slot : size_t {
        gOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gOutput", ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::CloudRayMarchBind
{
    /// @brief CloudRayMarch.CS.hlsl の契約
    enum Slot : size_t {
        gCloud,
        gAtmosphere,
        gBaseShapeNoise,
        gDetailNoise,
        gWeatherMap,
        gSceneDepth,
        gTransmittanceLUT,
        gSkyViewLUT,
        gCloudOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gCloud",            ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gAtmosphere",       ShaderBindingType::CBV, BindingUsage::Required },  // b1
        { "gBaseShapeNoise",   ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gDetailNoise",      ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gWeatherMap",       ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gSceneDepth",       ShaderBindingType::SRV, BindingUsage::Required },  // t3
        { "gTransmittanceLUT", ShaderBindingType::SRV, BindingUsage::Required },  // t4
        { "gSkyViewLUT",       ShaderBindingType::SRV, BindingUsage::Required },  // t5
        { "gCloudOutput",      ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::CloudCompositeBind
{
    /// @brief CloudComposite.CS.hlsl の契約
    enum Slot : size_t {
        gCloud,
        gSceneColor,
        gCloudBuffer,
        gSceneDepth,
        gOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gCloud",       ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gSceneColor",  ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gCloudBuffer", ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gSceneDepth",  ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gOutput",      ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::CloudCubemapCaptureBind
{
    /// @brief CloudCubemapCapture.CS.hlsl の契約
    enum Slot : size_t {
        gCloud,
        gAtmosphere,
        gBaseShapeNoise,
        gDetailNoise,
        gWeatherMap,
        gTransmittanceLUT,
        gSkyViewLUT,
        gSkyCubemap,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gCloud",            ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gAtmosphere",       ShaderBindingType::CBV, BindingUsage::Required },  // b1
        { "gBaseShapeNoise",   ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gDetailNoise",      ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gWeatherMap",       ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gTransmittanceLUT", ShaderBindingType::SRV, BindingUsage::Required },  // t4
        { "gSkyViewLUT",       ShaderBindingType::SRV, BindingUsage::Required },  // t5
        { "gSkyCubemap",       ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::CloudShadowMapBind
{
    /// @brief CloudShadowMap.CS.hlsl の契約
    enum Slot : size_t {
        gCloud,
        gGodRay,
        gBaseShapeNoise,
        gDetailNoise,
        gWeatherMap,
        gCloudShadowMap,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gCloud",          ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gGodRay",         ShaderBindingType::CBV, BindingUsage::Required },  // b1
        { "gBaseShapeNoise", ShaderBindingType::SRV, BindingUsage::Required },  // t0
        // t1: SampleCloudDensity へ渡す引数としてだけ宣言されている。この CS は
        // cheap 密度しか評価せずディテール分岐に入らないため、DXC が未参照として削除する
        { "gDetailNoise",    ShaderBindingType::SRV, BindingUsage::Optional },
        { "gWeatherMap",     ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gCloudShadowMap", ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::GodRayMarchBind
{
    /// @brief GodRayMarch.CS.hlsl の契約
    enum Slot : size_t {
        gGodRay,
        gAtmosphere,
        gCloudShadowMap,
        gTransmittanceLUT,
        gSceneDepth,
        gCloudBuffer,
        gGodRayOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gGodRay",           ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gAtmosphere",       ShaderBindingType::CBV, BindingUsage::Required },  // b1
        { "gCloudShadowMap",   ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gTransmittanceLUT", ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gSceneDepth",       ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gCloudBuffer",      ShaderBindingType::SRV, BindingUsage::Required },  // t3
        { "gGodRayOutput",     ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::GodRayCompositeBind
{
    /// @brief GodRayComposite.CS.hlsl の契約
    enum Slot : size_t {
        gGodRay,
        gSceneColor,
        gGodRayBuffer,
        gOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gGodRay",       ShaderBindingType::CBV, BindingUsage::Required },  // b0
        { "gSceneColor",   ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gGodRayBuffer", ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gOutput",       ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
