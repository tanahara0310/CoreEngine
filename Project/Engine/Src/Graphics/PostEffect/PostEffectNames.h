#pragma once

/// @brief ポストエフェクト名を一元管理する名前空間
/// @details エフェクト登録と取得で同じ文字列を使用するため、タイポを防ぐ

namespace CoreEngine
{
namespace PostEffectNames {
    constexpr const char* FullScreen = "FullScreen";
    constexpr const char* FadeEffect = "FadeEffect";
    constexpr const char* GrayScale = "GrayScale";
    constexpr const char* Blur = "Blur";
    constexpr const char* RadialBlur = "RadialBlur";
    constexpr const char* Shockwave = "Shockwave";
    constexpr const char* Vignette = "Vignette";
    constexpr const char* ColorGrading = "ColorGrading";
    constexpr const char* ChromaticAberration = "ChromaticAberration";
    constexpr const char* Sepia = "Sepia";
    constexpr const char* Invert = "Invert";
    constexpr const char* RasterScroll = "RasterScroll";
    constexpr const char* Bloom = "Bloom";
    constexpr const char* Dissolve = "Dissolve";
}
}
