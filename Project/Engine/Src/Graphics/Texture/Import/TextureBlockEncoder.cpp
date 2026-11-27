#include "pch.h"
#include "TextureBlockEncoder.h"

#include <Externals/bc7enc_rdo/bc7enc.h>
#include <Externals/bc7enc_rdo/rgbcx.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace CoreEngine
{
    namespace
    {
        /// @brief BC1・BC3 の色の探し方の細かさ（0〜18。大きいほど遅く、きれい）
        constexpr uint32_t kColorLevel = 8;

        /// @brief BC7 の探し方の細かさ（0〜4。大きいほど遅く、きれい）
        constexpr uint32_t kBc7Level = 4;

        /// @brief 1 ブロック（4x4 画素・RGBA）のバイト数
        constexpr uint32_t kBlockPixelBytes = 4 * 4 * 4;

        /// @brief 圧縮の表を最初の 1 回だけ作る
        void InitializeTables()
        {
            static std::once_flag once;
            std::call_once(once, [] {
                rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
                bc7enc_compress_block_init();
            });
        }

        /// @brief 圧縮した 1 ブロックのバイト数
        uint32_t BlockBytes(TextureBlockEncoder::Format format)
        {
            return format == TextureBlockEncoder::Format::BC1 ? 8u : 16u;
        }

        /// @brief 1 ブロック分の画素を左上から 1 行ずつ集める（はみ出す画素は端の画素を繰り返す）
        void GatherBlock(const uint8_t* pixels, size_t rowPitch, uint32_t width, uint32_t height,
            uint32_t blockX, uint32_t blockY, uint8_t* block)
        {
            for (uint32_t y = 0; y < 4; ++y) {
                const uint8_t* row = pixels + std::min(blockY * 4 + y, height - 1) * rowPitch;
                for (uint32_t x = 0; x < 4; ++x) {
                    std::memcpy(block + (y * 4 + x) * 4, row + std::min(blockX * 4 + x, width - 1) * 4, 4);
                }
            }
        }
    }

    void TextureBlockEncoder::Encode(const uint8_t* pixels, size_t rowPitch, uint32_t width, uint32_t height,
        Format format, uint8_t* destination, size_t destinationRowPitch)
    {
        if (width == 0 || height == 0) {
            return;
        }
        InitializeTables();

        bc7enc_compress_block_params bc7Params{};
        bc7enc_compress_block_params_init(&bc7Params);
        bc7enc_compress_block_params_init_linear_weights(&bc7Params);
        bc7Params.m_max_partitions = BC7ENC_MAX_PARTITIONS;
        bc7Params.m_uber_level = kBc7Level;

        const uint32_t blockBytes = BlockBytes(format);
        const uint32_t blocksX = (width + 3) / 4;
        const uint32_t blocksY = (height + 3) / 4;
        uint8_t block[kBlockPixelBytes];
        for (uint32_t by = 0; by < blocksY; ++by) {
            uint8_t* row = destination + by * destinationRowPitch;
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                GatherBlock(pixels, rowPitch, width, height, bx, by, block);
                uint8_t* encoded = row + bx * blockBytes;
                switch (format) {
                case Format::BC1:
                    rgbcx::encode_bc1(kColorLevel, encoded, block, true, false);
                    break;
                case Format::BC3:
                    rgbcx::encode_bc3(kColorLevel, encoded, block);
                    break;
                case Format::BC7:
                    bc7enc_compress_block(encoded, block, &bc7Params);
                    break;
                }
            }
        }
    }
}
