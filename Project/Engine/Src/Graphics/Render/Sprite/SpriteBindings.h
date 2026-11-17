#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief 標準のスプライトシェーダーが要求するリソースの契約
/// @details 添字は enum Slot、実体は SpriteRenderer が持つ BindingTable（描画中に名前で引かない）。

namespace CoreEngine::SpriteBind
{
    /// @brief Sprite.VS.hlsl / Sprite.PS.hlsl の契約
    enum Slot : size_t {
        gMaterial,
        TransformationMatrix,
        gTexture,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gMaterial",            ShaderBindingType::CBV, BindingUsage::Required },
        { "TransformationMatrix", ShaderBindingType::CBV, BindingUsage::Required },
        { "gTexture",             ShaderBindingType::SRV, BindingUsage::Required },
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
