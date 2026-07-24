#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief RenderGraph が扱う描画ビュー種別
    /// @details RenderPass.h から独立させた軽量ヘッダ。描画チェーン（RenderManager /
    ///          GameObject / Model）がパス基盤へ依存せずビュー種別を参照できるようにする。
    enum class RenderViewType : uint32_t {
        GameView = 0,
        ReflectionView = 1,
        CaptureView = 2,
    };
}
