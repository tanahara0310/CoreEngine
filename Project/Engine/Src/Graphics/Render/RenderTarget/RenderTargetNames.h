#pragma once

/// @brief レンダーターゲット名を一元管理する名前空間
/// @details レンダーターゲットの登録と取得で同じ文字列を使用するため、タイポを防ぐ

namespace CoreEngine
{
    namespace RenderTargetNames {
        constexpr const char* SceneColor = "SceneColor";
        constexpr const char* SceneColorSnapshot = "SceneColorSnapshot";
        constexpr const char* PostEffectIntermediatePrefix = "PostEffectIntermediate";
        constexpr const char* PostEffectFinal = "PostEffectFinal";
        constexpr const char* BackBuffer = "BackBuffer";
        constexpr const char* ReflectionView = "ReflectionView";
        constexpr const char* SSAOBuffer = "SSAOBuffer";
        constexpr const char* SSAOBlurBuffer = "SSAOBlurBuffer";
        constexpr const char* WaterCausticsBuffer = "WaterCausticsBuffer";
    }
}
