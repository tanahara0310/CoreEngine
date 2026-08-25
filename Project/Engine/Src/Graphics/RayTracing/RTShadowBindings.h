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
        ShadowRayConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gShadowOutput",      ShaderBindingType::UAV, BindingUsage::Required },  // u0
        { "gScene",             ShaderBindingType::SRV, BindingUsage::Required },  // t0: TLAS
        { "gSceneDepth",        ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gNormalRoughness",   ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "ShadowRayConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::RTShadowDenoiseBind
{
    /// @brief RTShadowDenoise.hlsl（A-Trous 空間デノイズ）の契約
    enum Slot : size_t {
        gInputShadow,
        gNormalRoughness,
        gSceneDepth,
        gOutputShadow,
        DenoiseConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gInputShadow",     ShaderBindingType::SRV, BindingUsage::Required },  // t0: トレース解像度
        { "gNormalRoughness", ShaderBindingType::SRV, BindingUsage::Required },  // t1: フル解像度
        { "gSceneDepth",      ShaderBindingType::SRV, BindingUsage::Required },  // t2: フル解像度
        { "gOutputShadow",    ShaderBindingType::UAV, BindingUsage::Required },  // u0: トレース解像度
        { "DenoiseConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::RTShadowTemporalBind
{
    /// @brief RTShadowTemporal.CS.hlsl（テンポラル蓄積）の契約
    enum Slot : size_t {
        gRawShadow,
        gGBufferNormal,
        gGBufferDepth,
        gHistoryShadow,
        gMotionVector,
        gOutputShadow,
        TemporalConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gRawShadow",        ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gGBufferNormal",    ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gGBufferDepth",     ShaderBindingType::SRV, BindingUsage::Required },  // t2
        { "gHistoryShadow",    ShaderBindingType::SRV, BindingUsage::Required },  // t3: 前フレームの履歴
        { "gMotionVector",     ShaderBindingType::SRV, BindingUsage::Required },  // t4
        { "gOutputShadow",     ShaderBindingType::UAV, BindingUsage::Required },  // u0: 今フレームの履歴
        { "TemporalConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::RTShadowResolveBind
{
    /// @brief RTShadowResolve.CS.hlsl（トレース解像度 → フル解像度のバイラテラルアップサンプル）の契約
    enum Slot : size_t {
        gTraceShadow,
        gSceneDepth,
        gNormalRoughness,
        gOutputShadow,
        ResolveConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gTraceShadow",     ShaderBindingType::SRV, BindingUsage::Required },  // t0: トレース解像度シャドウ
        { "gSceneDepth",      ShaderBindingType::SRV, BindingUsage::Required },  // t1: フル解像度 深度
        { "gNormalRoughness", ShaderBindingType::SRV, BindingUsage::Required },  // t2: フル解像度 法線
        { "gOutputShadow",    ShaderBindingType::UAV, BindingUsage::Required },  // u0: フル解像度 最終マスク
        { "ResolveConstants", ShaderBindingType::CBV, BindingUsage::Required },  // b0（ルート定数）
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
