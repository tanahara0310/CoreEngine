#pragma once

//========================================================================================
// ModelBindings.h
//
// モデル描画シェーダー 4 種（通常／スキニング × フォワード／G-Buffer）が要求する
// リソースの契約。enum は 4 つの表で共有し、そのシェーダーに無いものを Optional にする。
//
// Usage の使い分け:
//   Required    … そのシェーダーに必ず存在する
//   Conditional … 存在するが、差すかはマテリアル・機能トグル次第
//   Optional    … そのシェーダーには無い（もしくは無くてよい）
//
// 【実測メモ 2026-08-24】
//   gEnvironmentTexture と gMatrixPalette は 4 種すべてのルートシグネチャに存在しない。
//   前者は IBL 3 枚（Irradiance / Prefiltered / BRDF LUT）に置き換わり、後者はスキニングが
//   コンピュートシェーダーへ移った（VS は変換済み頂点を読む）ため。
//   engine 側のバインドは `if (idx >= 0)` で黙って skip されていた。
//   カスタムシェーダーが使う可能性があるので Optional として残してある。
//
// 詳細: Docs/Engine/Graphics/Shader/ShaderBinding_Design_Review.md §4.4
//========================================================================================

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::ModelBind
{
    /// @brief 4 つの宣言表で共有する添字
    enum Slot : size_t {
        gCamera,
        gLightCounts,
        gDirectionalLights,
        gPointLights,
        gSpotLights,
        gAreaLights,
        gEnvironmentTexture,
        gRTShadowMask,
        gIrradianceMap,
        gPrefilteredMap,
        gBRDFLUT,
        gIBLParams,
        gTransformationMatrix,
        gInstanceData,
        gMaterial,
        gTexture,
        gNormalMap,
        gMetallicRoughnessMap,
        gEmissiveMap,
        gAOMap,
        gMatrixPalette,
        Count
    };

    // 表記を短くするための別名
    constexpr auto kReq = BindingUsage::Required;
    constexpr auto kCond = BindingUsage::Conditional;
    constexpr auto kOpt = BindingUsage::Optional;
    constexpr auto kCBV = ShaderBindingType::CBV;
    constexpr auto kSRV = ShaderBindingType::SRV;

    /// @brief Object3d.VS/PS（通常モデル・フォワード）
    inline constexpr ShaderBindingDecl kForward[] = {
        { "gCamera",               kCBV, kReq  },
        { "gLightCounts",          kCBV, kReq  },
        { "gDirectionalLights",    kSRV, kReq  },
        { "gPointLights",          kSRV, kReq  },
        { "gSpotLights",           kSRV, kReq  },
        { "gAreaLights",           kSRV, kReq  },
        { "gEnvironmentTexture",   kSRV, kOpt  },  // 実体なし（IBL 3 枚に置換済み）
        { "gRTShadowMask",         kSRV, kCond },  // レイトレ OFF のフレームは差さない
        { "gIrradianceMap",        kSRV, kCond },
        { "gPrefilteredMap",       kSRV, kCond },
        { "gBRDFLUT",              kSRV, kCond },
        { "gIBLParams",            kCBV, kCond },
        { "gTransformationMatrix", kCBV, kOpt  },  // スキニング版のみ
        { "gInstanceData",         kSRV, kReq  },
        { "gMaterial",             kCBV, kReq  },
        { "gTexture",              kSRV, kReq  },
        { "gNormalMap",            kSRV, kCond },  // マテリアルが持たなければ差さない
        { "gMetallicRoughnessMap", kSRV, kCond },
        { "gEmissiveMap",          kSRV, kCond },
        { "gAOMap",                kSRV, kCond },
        { "gMatrixPalette",        kSRV, kOpt  },  // 実体なし（スキニングは CS 側）
    };

    /// @brief GBuffer.VS/PS（通常モデル・G-Buffer）
    /// @note カメラとライトは G-Buffer では不要（ライティングは DeferredLighting が行う）
    inline constexpr ShaderBindingDecl kGBuffer[] = {
        { "gCamera",               kCBV, kOpt  },
        { "gLightCounts",          kCBV, kOpt  },
        { "gDirectionalLights",    kSRV, kOpt  },
        { "gPointLights",          kSRV, kOpt  },
        { "gSpotLights",           kSRV, kOpt  },
        { "gAreaLights",           kSRV, kOpt  },
        { "gEnvironmentTexture",   kSRV, kOpt  },
        { "gRTShadowMask",         kSRV, kOpt  },
        { "gIrradianceMap",        kSRV, kOpt  },
        { "gPrefilteredMap",       kSRV, kOpt  },
        { "gBRDFLUT",              kSRV, kOpt  },
        { "gIBLParams",            kCBV, kOpt  },
        { "gTransformationMatrix", kCBV, kOpt  },
        { "gInstanceData",         kSRV, kReq  },
        { "gMaterial",             kCBV, kReq  },
        { "gTexture",              kSRV, kReq  },
        { "gNormalMap",            kSRV, kCond },
        { "gMetallicRoughnessMap", kSRV, kCond },
        { "gEmissiveMap",          kSRV, kCond },
        { "gAOMap",                kSRV, kCond },
        { "gMatrixPalette",        kSRV, kOpt  },
    };

    /// @brief スキニングモデル・フォワード（gInstanceData の代わりに gTransformationMatrix）
    inline constexpr ShaderBindingDecl kSkinnedForward[] = {
        { "gCamera",               kCBV, kReq  },
        { "gLightCounts",          kCBV, kReq  },
        { "gDirectionalLights",    kSRV, kReq  },
        { "gPointLights",          kSRV, kReq  },
        { "gSpotLights",           kSRV, kReq  },
        { "gAreaLights",           kSRV, kReq  },
        { "gEnvironmentTexture",   kSRV, kOpt  },
        { "gRTShadowMask",         kSRV, kCond },
        { "gIrradianceMap",        kSRV, kCond },
        { "gPrefilteredMap",       kSRV, kCond },
        { "gBRDFLUT",              kSRV, kCond },
        { "gIBLParams",            kCBV, kCond },
        { "gTransformationMatrix", kCBV, kReq  },
        { "gInstanceData",         kSRV, kOpt  },  // 通常モデル版のみ
        { "gMaterial",             kCBV, kReq  },
        { "gTexture",              kSRV, kReq  },
        { "gNormalMap",            kSRV, kCond },
        { "gMetallicRoughnessMap", kSRV, kCond },
        { "gEmissiveMap",          kSRV, kCond },
        { "gAOMap",                kSRV, kCond },
        { "gMatrixPalette",        kSRV, kOpt  },
    };

    /// @brief スキニングモデル・G-Buffer
    inline constexpr ShaderBindingDecl kSkinnedGBuffer[] = {
        { "gCamera",               kCBV, kOpt  },
        { "gLightCounts",          kCBV, kOpt  },
        { "gDirectionalLights",    kSRV, kOpt  },
        { "gPointLights",          kSRV, kOpt  },
        { "gSpotLights",           kSRV, kOpt  },
        { "gAreaLights",           kSRV, kOpt  },
        { "gEnvironmentTexture",   kSRV, kOpt  },
        { "gRTShadowMask",         kSRV, kOpt  },
        { "gIrradianceMap",        kSRV, kOpt  },
        { "gPrefilteredMap",       kSRV, kOpt  },
        { "gBRDFLUT",              kSRV, kOpt  },
        { "gIBLParams",            kCBV, kOpt  },
        { "gTransformationMatrix", kCBV, kReq  },
        { "gInstanceData",         kSRV, kOpt  },
        { "gMaterial",             kCBV, kReq  },
        { "gTexture",              kSRV, kReq  },
        { "gNormalMap",            kSRV, kCond },
        { "gMetallicRoughnessMap", kSRV, kCond },
        { "gEmissiveMap",          kSRV, kCond },
        { "gAOMap",                kSRV, kCond },
        { "gMatrixPalette",        kSRV, kOpt  },
    };

    /// @brief カスタムシェーダー用（すべて Optional）
    /// @details アプリ側が書いたシェーダーに対してエンジンが何かを「必須」にはできない。
    ///          宣言されているものだけエンジンが差す、という関係を表す表。
    ///          これを CustomShaderPipeline が構築時に 1 回解決することで、
    ///          描画中の名前引き（1 ドローあたり 8〜9 回の map 検索）が消える。
    inline constexpr ShaderBindingDecl kCustom[] = {
        { "gCamera",               kCBV, kOpt },
        { "gLightCounts",          kCBV, kOpt },
        { "gDirectionalLights",    kSRV, kOpt },
        { "gPointLights",          kSRV, kOpt },
        { "gSpotLights",           kSRV, kOpt },
        { "gAreaLights",           kSRV, kOpt },
        { "gEnvironmentTexture",   kSRV, kOpt },
        { "gRTShadowMask",         kSRV, kOpt },
        { "gIrradianceMap",        kSRV, kOpt },
        { "gPrefilteredMap",       kSRV, kOpt },
        { "gBRDFLUT",              kSRV, kOpt },
        { "gIBLParams",            kCBV, kOpt },
        { "gTransformationMatrix", kCBV, kOpt },
        { "gInstanceData",         kSRV, kOpt },
        { "gMaterial",             kCBV, kOpt },
        { "gTexture",              kSRV, kOpt },
        { "gNormalMap",            kSRV, kOpt },
        { "gMetallicRoughnessMap", kSRV, kOpt },
        { "gEmissiveMap",          kSRV, kOpt },
        { "gAOMap",                kSRV, kOpt },
        { "gMatrixPalette",        kSRV, kOpt },
    };

    static_assert(std::size(kCustom) == Slot::Count, "kCustom と Slot の並びがずれている");
    static_assert(std::size(kForward) == Slot::Count, "kForward と Slot の並びがずれている");
    static_assert(std::size(kGBuffer) == Slot::Count, "kGBuffer と Slot の並びがずれている");
    static_assert(std::size(kSkinnedForward) == Slot::Count, "kSkinnedForward と Slot の並びがずれている");
    static_assert(std::size(kSkinnedGBuffer) == Slot::Count, "kSkinnedGBuffer と Slot の並びがずれている");
}
