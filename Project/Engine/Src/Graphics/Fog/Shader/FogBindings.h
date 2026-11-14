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
        gVolumetricFog,
        gSceneDepth,
        gSkyViewLUT,
        gVolumetricFogIntegrated,
        gVolumetricFogFarLight,
        gCloudShadowMap,
        gSunDepth,
        gOutput,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gFog",        ShaderBindingType::CBV, BindingUsage::Required },  // b0
        // ボリュメトリックの格子を使うフレームだけ差す
        { "gVolumetricFog", ShaderBindingType::CBV, BindingUsage::Conditional }, // b1
        { "gSceneDepth", ShaderBindingType::SRV, BindingUsage::Required },     // t0
        // 空色ブレンドが有効なフレームだけ差す（大気非対応シーンでは未バインド）
        { "gSkyViewLUT", ShaderBindingType::SRV, BindingUsage::Conditional }, // t1
        { "gVolumetricFogIntegrated", ShaderBindingType::SRV, BindingUsage::Conditional }, // t2
        { "gVolumetricFogFarLight", ShaderBindingType::SRV, BindingUsage::Conditional }, // t3
        // ボリュメトリックの格子を使い、雲もあるフレームだけ差す
        { "gCloudShadowMap", ShaderBindingType::SRV, BindingUsage::Conditional }, // t4
        // ボリュメトリックの格子を使い、太陽から見た深さの画像を作ったフレームだけ差す（デバッグ表示 4・5 が読む）
        { "gSunDepth",       ShaderBindingType::SRV, BindingUsage::Conditional }, // t5
        { "gOutput",     ShaderBindingType::UAV, BindingUsage::Required },  // u0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
