#pragma once

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
        gDeferredView,
        gLightCounts,
        gDirectionalLights,
        gPointLights,
        gSpotLights,
        gAreaLights,
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
        gCloudShadowMap,
        gCloudShadow,
        gGBufferMotion,
        gWetSurface,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        // G-Buffer と深度。これが無いとライティングが成立しないので毎フレーム必須
        { "gAlbedoAO",            ShaderBindingType::SRV, BindingUsage::Required    },
        { "gNormalRoughness",     ShaderBindingType::SRV, BindingUsage::Required    },
        { "gEmissiveMetallic",    ShaderBindingType::SRV, BindingUsage::Required    },
        { "gSceneDepth",          ShaderBindingType::SRV, BindingUsage::Required    },

        // カメラとビューごとの定数（深度復元行列・背景色）
        { "gCamera",              ShaderBindingType::CBV, BindingUsage::Required    },
        { "gDeferredView",        ShaderBindingType::CBV, BindingUsage::Required    },

        // ライト（LightManager が差す。ビュー種別に関わらず毎フレーム）
        { "gLightCounts",         ShaderBindingType::CBV, BindingUsage::Required    },
        { "gDirectionalLights",   ShaderBindingType::SRV, BindingUsage::Required    },
        { "gPointLights",         ShaderBindingType::SRV, BindingUsage::Required    },
        { "gSpotLights",          ShaderBindingType::SRV, BindingUsage::Required    },
        { "gAreaLights",          ShaderBindingType::SRV, BindingUsage::Required    },

        // RT シャドウマスク（無いフレームは 1×1 の白を差す）
        { "gRTShadowMask0",       ShaderBindingType::SRV, BindingUsage::Required    },
        { "gRTShadowMask1",       ShaderBindingType::SRV, BindingUsage::Required    },
        { "gRTShadowMask2",       ShaderBindingType::SRV, BindingUsage::Required    },
        { "gRTShadowMask3",       ShaderBindingType::SRV, BindingUsage::Required    },

        // SSAO・水中コースティクス（無いフレームは 1×1 の白を差す）
        { "gSSAO",                ShaderBindingType::SRV, BindingUsage::Required    },
        { "gWaterCaustics",       ShaderBindingType::SRV, BindingUsage::Required    },
        { "gWaterCausticsDebug",  ShaderBindingType::CBV, BindingUsage::Required    },

        // 空アンビエント／空スペキュラ（大気の LUT 生成待ちのフレームは差さない）
        { "gSkyAmbient",          ShaderBindingType::CBV, BindingUsage::Conditional },
        { "gSkyIrradianceSH",     ShaderBindingType::SRV, BindingUsage::Conditional },
        { "gSkySpecularMap",      ShaderBindingType::SRV, BindingUsage::Conditional },

        // 雲シャドウ（雲の無いフレームは 1×1 の白と強さ 0 の CB を差す）
        { "gCloudShadowMap",      ShaderBindingType::SRV, BindingUsage::Required    },
        { "gCloudShadow",         ShaderBindingType::CBV, BindingUsage::Required    },

        // 濡れ跡（動く物の印を読む G-Buffer のモーションベクターと、高さごとの濡れ具合の表）。毎フレーム差す
        { "gGBufferMotion",       ShaderBindingType::SRV, BindingUsage::Required    },
        { "gWetSurface",          ShaderBindingType::CBV, BindingUsage::Required    },
    };

    static_assert(std::size(kDecls) == Slot::Count,
        "kDecls と Slot の並びがずれている");
}
