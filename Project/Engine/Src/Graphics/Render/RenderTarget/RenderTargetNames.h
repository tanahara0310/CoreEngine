#pragma once

/// @brief レンダーターゲット名を一元管理する名前空間
/// @details レンダーターゲットの登録と取得で同じ文字列を使用するため、タイポを防ぐ

namespace CoreEngine
{
namespace RenderTargetNames {
    constexpr const char* BackBuffer  = "BackBuffer";
    constexpr const char* Offscreen0  = "Offscreen0";
    constexpr const char* Offscreen1  = "Offscreen1";
    constexpr const char* SSAOBuffer  = "SSAOBuffer";
    constexpr const char* SSAOBlurBuffer = "SSAOBlurBuffer";
}
}
