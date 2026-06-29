#pragma once

/// @brief レンダリング技術名を一元管理する名前空間
/// @details SSAO、TAA、SSRなどの高度なレンダリング技術の名前を管理
///          ポストエフェクト（色調補正など）とは区別するため独立した名前空間とする

namespace CoreEngine
{
namespace RenderingTechniqueNames {
    constexpr const char* DeferredLighting = "DeferredLighting";
    constexpr const char* WaterCaustics = "WaterCaustics";
    constexpr const char* SSAO      = "SSAO";
    constexpr const char* SSAOBlur  = "SSAOBlur";
    constexpr const char* TAA       = "TAA";
    // 将来的な拡張用
    // constexpr const char* SSR       = "SSR";
    // constexpr const char* RTAO      = "RTAO";
    // constexpr const char* GTAO      = "GTAO";
}
}
