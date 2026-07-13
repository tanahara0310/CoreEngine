#pragma once

namespace CoreEngine
{
    /// @brief テクスチャの色空間指定
    /// @note PBR ではベースカラー/エミッシブのみ sRGB、
    ///       法線・MetallicRoughness・AO などのデータテクスチャはリニアで読み込む必要がある。
    ///       sRGB として読むとサンプリング時にガンマデコードされ、データ値が歪む。
    enum class TextureColorSpace {
        SRGB,   ///< カラー画像（ベースカラー・エミッシブ・UIなど）。ガンマデコードあり
        Linear, ///< データ画像（法線・MetallicRoughness・AO・マスクなど）。補正なし
    };
}
