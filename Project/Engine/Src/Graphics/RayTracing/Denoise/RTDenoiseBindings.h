#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::RTDenoiseTemporalBind
{
    /// @brief RTDenoiseTemporal.CS.hlsl（時間方向の蓄積）の契約
    enum Slot : size_t {
        gRawSignal,
        gGBufferNormal,
        gGBufferDepth,
        gHistorySignal,
        gMotionVector,
        gOutputSignal,
        RTDenoiseTemporalConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gRawSignal",                 ShaderBindingType::SRV, BindingUsage::Required },  // t0: 入力
        { "gGBufferNormal",             ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gGBufferDepth",              ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gHistorySignal",             ShaderBindingType::SRV, BindingUsage::Required },  // t3: 前フレームの履歴
        { "gMotionVector",              ShaderBindingType::SRV, BindingUsage::Required },  // t4
        { "gOutputSignal",              ShaderBindingType::UAV, BindingUsage::Required },  // u0: 今フレームの履歴
        { "RTDenoiseTemporalConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::RTDenoiseATrousBind
{
    /// @brief RTDenoiseATrous.CS.hlsl（A-Trous）の契約
    enum Slot : size_t {
        gInputSignal,
        gNormalRoughness,
        gSceneDepth,
        gOutputSignal,
        RTDenoiseATrousConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gInputSignal",             ShaderBindingType::SRV, BindingUsage::Required },  // t0: トレース解像度
        { "gNormalRoughness",         ShaderBindingType::SRV, BindingUsage::Required },  // t1: フル解像度
        { "gSceneDepth",              ShaderBindingType::SRV, BindingUsage::Required },  // t2: フル解像度
        { "gOutputSignal",            ShaderBindingType::UAV, BindingUsage::Required },  // u0: トレース解像度
        { "RTDenoiseATrousConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::RTDenoiseResolveBind
{
    /// @brief RTDenoiseResolve.CS.hlsl（トレース解像度 → フル解像度）の契約
    enum Slot : size_t {
        gTraceSignal,
        gSceneDepth,
        gNormalRoughness,
        gOutputSignal,
        RTDenoiseResolveConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gTraceSignal",              ShaderBindingType::SRV, BindingUsage::Required },  // t0: トレース解像度
        { "gSceneDepth",               ShaderBindingType::SRV, BindingUsage::Required },  // t1: フル解像度
        { "gNormalRoughness",          ShaderBindingType::SRV, BindingUsage::Required },  // t2: フル解像度
        { "gOutputSignal",             ShaderBindingType::UAV, BindingUsage::Required },  // u0: フル解像度の出力
        { "RTDenoiseResolveConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
