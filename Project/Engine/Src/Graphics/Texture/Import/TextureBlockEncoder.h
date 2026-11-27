#pragma once

#include <cstddef>
#include <cstdint>

namespace CoreEngine
{
    /// @brief RGBA 各 8 ビットの画像を BC1・BC3・BC7 のブロックへ圧縮する（bc7enc_rdo の rgbcx と bc7enc）
    /// @details 1 枚の圧縮は呼んだスレッドだけで行う。別々の画像なら複数のスレッドから同時に呼べる。
    namespace TextureBlockEncoder
    {
        /// @brief 圧縮の形式
        enum class Format
        {
            BC1, ///< RGB（透明なし）
            BC3, ///< RGB と A
            BC7, ///< RGBA（モード 1・5・6・7）
        };

        /// @brief 画像を圧縮する
        /// @param pixels 左上から 1 行ずつ並んだ画素（1 画素 4 バイト。R・G・B・A の順）
        /// @param rowPitch pixels の 1 行のバイト数
        /// @param width 画像の幅（4 の倍数でなくてよい。はみ出す画素は端の画素を繰り返す）
        /// @param height 画像の高さ
        /// @param format 圧縮の形式
        /// @param destination ブロックの書き先（ブロックの行を上から順に書く）
        /// @param destinationRowPitch destination の 1 行（横に並ぶブロック）のバイト数
        void Encode(const uint8_t* pixels, size_t rowPitch, uint32_t width, uint32_t height,
            Format format, uint8_t* destination, size_t destinationRowPitch);
    }
}
