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
        Count
    };

    inline constexpr auto kCond = BindingUsage::Conditional;
    inline constexpr auto kOpt = BindingUsage::Optional;
    inline constexpr auto kCBV = ShaderBindingType::CBV;
    inline constexpr auto kSRV = ShaderBindingType::SRV;

    inline constexpr ShaderBindingDecl kDecls[] = {
        // 水面本体の定数
        // WaterConstants(b4) は Water.VS.hlsl だけが持つ。FFTWater.VS.hlsl は波を
        // FFT テクスチャから取るので宣言していない（＝ルートシグネチャに載らない）。
        // 【2026-08-24 実測】この事実は契約導入で初めて分かった。従来は
        //   `if (idx >= 0)` で無言に skip されており、どちらの経路か区別できなかった。
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
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
