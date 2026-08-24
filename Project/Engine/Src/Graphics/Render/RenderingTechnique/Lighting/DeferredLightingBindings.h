#pragma once

//========================================================================================
// DeferredLightingBindings.h
//
// DeferredLighting.PS.hlsl が要求するリソースの契約。
// enum の並びと kDecls の並びは 1 対 1（static_assert で縛っている）。
//
// Usage の使い分け:
//   Required    … 毎フレーム必ず差す。差し忘れは Draw 前に検出される
//   Conditional … シェーダーには必須だが、差すかはフレーム次第
//                 （RT シャドウ OFF、IBL 未生成、空スペキュラのウォームアップ待ち等）
//   Optional    … シェーダー側から消えてもよい
//
// 詳細: Docs/Engine/Graphics/Shader/ShaderBinding_Design_Review.md §4.4
//========================================================================================

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::DeferredLightingBind
{
    /// @brief kDecls の添字。並びを変えるときは kDecls も同時に動かすこと
    enum Slot : size_t {
        gAlbedoAO,
        gNormalRoughness,
        gEmissiveMetallic,
        gSceneDepth,
        gCamera,
        gDepthReconstruction,
        gLightCounts,
        gDirectionalLights,
        gPointLights,
        gSpotLights,
        gAreaLights,
        gIrradianceMap,
        gPrefilteredMap,
        gBRDFLUT,
        gIBLParams,
        gRTShadowMask0,
        gRTShadowMask1,
        gRTShadowMask2,
        gRTShadowMask3,
        gSSAO,
        gWaterCaustics,
        gWaterCausticsDebug,
        gSkyAmbient,
        gSkyIrradianceSH,
        gSkySpecularMap,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        // G-Buffer と深度。これが無いとライティングが成立しないので毎フレーム必須
        { "gAlbedoAO",            ShaderBindingType::SRV, BindingUsage::Required    },
        { "gNormalRoughness",     ShaderBindingType::SRV, BindingUsage::Required    },
        { "gEmissiveMetallic",    ShaderBindingType::SRV, BindingUsage::Required    },
        { "gSceneDepth",          ShaderBindingType::SRV, BindingUsage::Required    },

        // カメラと深度復元行列
        { "gCamera",              ShaderBindingType::CBV, BindingUsage::Required    },
        { "gDepthReconstruction", ShaderBindingType::CBV, BindingUsage::Required    },

        // ライト（LightManager が差す。ビュー種別に関わらず毎フレーム）
        { "gLightCounts",         ShaderBindingType::CBV, BindingUsage::Required    },
        { "gDirectionalLights",   ShaderBindingType::SRV, BindingUsage::Required    },
        { "gPointLights",         ShaderBindingType::SRV, BindingUsage::Required    },
        { "gSpotLights",          ShaderBindingType::SRV, BindingUsage::Required    },
        { "gAreaLights",          ShaderBindingType::SRV, BindingUsage::Required    },

        // IBL（RenderManager 側が未生成のフレームは差さない）
        { "gIrradianceMap",       ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gPrefilteredMap",      ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gBRDFLUT",             ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gIBLParams",           ShaderBindingType::CBV, BindingUsage::Conditional },

        // RT シャドウマスク（レイトレ OFF・ライト数不足のフレームは差さない）
        { "gRTShadowMask0",       ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gRTShadowMask1",       ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gRTShadowMask2",       ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gRTShadowMask3",       ShaderBindingType::SRV, BindingUsage::Conditional },

        // SSAO・水中コースティクス（機能トグル）
        { "gSSAO",                ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gWaterCaustics",       ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gWaterCausticsDebug",  ShaderBindingType::CBV, BindingUsage::Conditional },

        // 空アンビエント／空スペキュラ（大気の LUT 生成待ちのフレームは差さない）
        { "gSkyAmbient",          ShaderBindingType::CBV, BindingUsage::Conditional },
        { "gSkyIrradianceSH",     ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gSkySpecularMap",      ShaderBindingType::SRV, BindingUsage::Conditional },
    };

    static_assert(std::size(kDecls) == Slot::Count,
        "kDecls と Slot の並びがずれている");
}
