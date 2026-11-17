#pragma once

#include "Graphics/Shader/ShaderBindingContract.h"

#include <cstddef>
#include <iterator>

/// @file
/// @brief FFT の海の計算 CS（時間発展・IFFT・仕上げ）が要求するリソースの契約
/// @details 添字は enum Slot、実体は FFTOceanManager が BindingTable で持ち、FFTOceanDispatchHelper へ渡す。

namespace CoreEngine::FFTOceanEvolutionBind
{
    /// @brief FFTOceanTimeEvolution.CS.hlsl の契約
    enum Slot : size_t {
        gSpectrumSamples,
        gHeightDisplacementXOutput,
        gDisplacementZOutput,
        FFTOceanSimulationConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gSpectrumSamples",           ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gHeightDisplacementXOutput", ShaderBindingType::UAV, BindingUsage::Required },  // u0
        { "gDisplacementZOutput",       ShaderBindingType::UAV, BindingUsage::Required },  // u1
        // 定数のアドレスが 0 でないときだけ差す
        { "FFTOceanSimulationConstants", ShaderBindingType::CBV, BindingUsage::Conditional }, // b0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::FFTOceanIFFTBind
{
    /// @brief FFTOceanIFFT.CS.hlsl の契約
    enum Slot : size_t {
        gInputSpectrum,
        gOutputSpectrum,
        FFTOceanIFFTConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gInputSpectrum",  ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gOutputSpectrum", ShaderBindingType::UAV, BindingUsage::Required },  // u0
        // 定数のアドレスが 0 でないときだけ差す
        { "FFTOceanIFFTConstants", ShaderBindingType::CBV, BindingUsage::Conditional }, // b0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}

namespace CoreEngine::FFTOceanFinalizeBind
{
    /// @brief FFTOceanFinalize.CS.hlsl の契約
    enum Slot : size_t {
        gHeightDisplacementXSpectrum,
        gDisplacementZSpectrum,
        gDisplacementOutput,
        gNormalOutput,
        gJacobianOutput,
        FFTOceanSimulationConstants,
        Count
    };

    inline constexpr ShaderBindingDecl kDecls[] = {
        { "gHeightDisplacementXSpectrum", ShaderBindingType::SRV, BindingUsage::Required },  // t0
        { "gDisplacementZSpectrum",       ShaderBindingType::SRV, BindingUsage::Required },  // t1
        { "gDisplacementOutput",          ShaderBindingType::UAV, BindingUsage::Required },  // u0
        { "gNormalOutput",                ShaderBindingType::UAV, BindingUsage::Required },  // u1
        { "gJacobianOutput",              ShaderBindingType::UAV, BindingUsage::Required },  // u2
        // 定数のアドレスが 0 でないときだけ差す
        { "FFTOceanSimulationConstants",  ShaderBindingType::CBV, BindingUsage::Conditional }, // b0
    };

    static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
}
