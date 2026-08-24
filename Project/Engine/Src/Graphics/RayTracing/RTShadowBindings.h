#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::RTShadowBind
{
    /// @brief RTShadow.hlsl（lib_6_6）が要求するリソースの契約
    /// @note DXR はディスパッチのたびに全部差すので、すべて Required でよい。
    ///       lib_6_6 は未使用の宣言を削除しないため、シェーダーから消えたのに宣言が残ると
    ///       Required 違反として起動時に検出される（＝改名事故が無言で通らない）。
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
