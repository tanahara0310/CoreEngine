#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

namespace CoreEngine::WaterBind
{
    enum Slot : size_t {
        WaterConstants,
        WaterFrameConstants,
        gReflectionTexture,
        gSceneDepth,
        gSceneColor,
        gRTWaterRefractionColor,
        gFFTOceanDisplacement,
        gFFTOceanNormal,
        gFFTOceanJacobian,
        gFFTOceanFoam,
        gAtmosphereAP,
        gCameraVolumeLUT,
        gSkyViewLUTAP,
        gWaterSkyIrradianceSH,
        gSkyEnvironmentMap,
        gWaterSunVisibility,
        gWaterSeabedHeight,
        gWaterShoreFoam,
        gWaterSeaMap,
        gWaterRipple,
        gWaterObjectFoam,
        gWaterObjectFoamFlow,
        gWaterFoamNet,
        gWaterFoamGrain,
        gWaterShoreMeshDisplacement,
        gWaterShoreMeshSurface,
        Count
    };

    inline constexpr auto kCond = BindingUsage::Conditional;
    inline constexpr auto kOpt = BindingUsage::Optional;
    inline constexpr auto kCBV = ShaderBindingType::CBV;
    inline constexpr auto kSRV = ShaderBindingType::SRV;

    inline constexpr ShaderBindingDecl kDecls[] = {
        // 水面本体の定数
        // WaterConstants(b4) は Water.VS.hlsl だけが持つ
        // （FFTWater.VS.hlsl は波を FFT テクスチャから取るので宣言していない）
        { "WaterConstants",          kCBV, kOpt  },
        // WaterFrameConstants(b5) は共有の hlsli にあるので全バリアントに存在する
        { "WaterFrameConstants",     kCBV, kCond },

        // 反射・屈折・シーン参照（機能トグルとフレーム状況で差さないことがある）
        { "gReflectionTexture",      kSRV, kCond },
        { "gSceneDepth",             kSRV, kCond },
        { "gSceneColor",             kSRV, kCond },
        { "gRTWaterRefractionColor", kSRV, kCond },

        // FFT 海面（useFFTOcean=false のシェーダーには存在しない）
        { "gFFTOceanDisplacement",   kSRV, kOpt  },
        { "gFFTOceanNormal",         kSRV, kOpt  },
        { "gFFTOceanJacobian",       kSRV, kOpt  },
        { "gFFTOceanFoam",           kSRV, kOpt  },

        // 大気散乱（Aerial Perspective）
        { "gAtmosphereAP",           kCBV, kCond },
        { "gCameraVolumeLUT",        kSRV, kCond },
        { "gSkyViewLUTAP",           kSRV, kCond },

        // 空アンビエント SH・空スペキュラキューブマップ
        { "gWaterSkyIrradianceSH",   kSRV, kCond },
        { "gSkyEnvironmentMap",      kSRV, kCond },

        // 水面の日向率（RT 反射パスの 2 枚目の出力）
        { "gWaterSunVisibility",     kSRV, kCond },

        // 海底の高さ（カメラの周りの範囲を RT で真上から測ったもの）
        { "gWaterSeabedHeight",      kSRV, kCond },

        // 岸の泡（WaterShoreFoamPass の出力。海底の高さと同じ範囲）
        { "gWaterShoreFoam",         kSRV, kCond },

        // 海の範囲マップ（水面の範囲全体の水深と上の物までの高さ。頂点シェーダーが読む）
        { "gWaterSeaMap",            kSRV, kCond },

        // 水の波紋（カメラの前に置いた 3 段の格子の高さと傾き。頂点シェーダーとピクセルシェーダーが読む）
        { "gWaterRipple",            kSRV, kCond },

        // 物がかき回した所の泡と流れ（カメラの前に置いた格子。ピクセルシェーダーが読む）
        { "gWaterObjectFoam",        kSRV, kCond },
        { "gWaterObjectFoamFlow",    kSRV, kCond },

        // 泡の模様の画像（網目と気泡の粒。ピクセルシェーダーが読む）
        { "gWaterFoamNet",           kSRV, kCond },
        { "gWaterFoamGrain",         kSRV, kCond },

        // 岸の格子の頂点の変位と、沖から寄せる波の傾き・砕けた波の泡（WaterShoreMeshDisplacement.CS の出力。
        // FFTWater.VS だけが読む）
        { "gWaterShoreMeshDisplacement", kSRV, kOpt },
        { "gWaterShoreMeshSurface",      kSRV, kOpt },
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
