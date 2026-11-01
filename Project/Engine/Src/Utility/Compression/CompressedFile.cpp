#include "pch.h"
#include "CompressedFile.h"

#include <Externals/zstd/lib/zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>

namespace CoreEngine
{
    namespace
    {
        /// @brief zstd のフレームの先頭 4 バイト（リトルエンディアンの 0xFD2FB528）
        constexpr uint8_t kFrameMagic[4] = { 0x28, 0xB5, 0x2F, 0xFD };

        /// @brief ReadHead が 1 回に読む圧縮側の大きさ
        constexpr size_t kHeadReadChunk = 16 * 1024;

        struct DCtxDeleter
        {
            void operator()(ZSTD_DCtx* context) const { ZSTD_freeDCtx(context); }
        };
        struct CCtxDeleter
        {
            void operator()(ZSTD_CCtx* context) const { ZSTD_freeCCtx(context); }
        };

        /// @brief スレッドごとに使い回す展開の文脈
        ZSTD_DCtx* DecompressContext()
        {
            thread_local std::unique_ptr<ZSTD_DCtx, DCtxDeleter> context(ZSTD_createDCtx());
            ZSTD_DCtx_reset(context.get(), ZSTD_reset_session_only);
            return context.get();
        }

        /// @brief スレッドごとに使い回す圧縮の文脈
        ZSTD_CCtx* CompressContext()
        {
            thread_local std::unique_ptr<ZSTD_CCtx, CCtxDeleter> context(ZSTD_createCCtx());
            return context.get();
        }

        bool StartsWithMagic(const uint8_t* data, size_t size)
        {
            return size >= sizeof(kFrameMagic) && std::memcmp(data, kFrameMagic, sizeof(kFrameMagic)) == 0;
        }

        /// @brief ファイルを全部そのまま読む
        bool ReadRaw(const std::filesystem::path& path, std::vector<uint8_t>& data)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                return false;
            }
            const std::streamoff size = file.tellg();
            if (size < 0) {
                return false;
            }
            data.resize(static_cast<size_t>(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(data.data()), size);
            return static_cast<bool>(file) || data.empty();
        }
    }

    bool CompressedFile::Read(const std::filesystem::path& path, std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> raw;
        if (!ReadRaw(path, raw)) {
            return false;
        }
        if (!StartsWithMagic(raw.data(), raw.size())) {
            data = std::move(raw);
            return true;
        }

        const unsigned long long contentSize = ZSTD_getFrameContentSize(raw.data(), raw.size());
        if (contentSize == ZSTD_CONTENTSIZE_UNKNOWN || contentSize == ZSTD_CONTENTSIZE_ERROR) {
            return false;
        }
        data.resize(static_cast<size_t>(contentSize));
        const size_t written = ZSTD_decompressDCtx(DecompressContext(), data.data(), data.size(), raw.data(), raw.size());
        return !ZSTD_isError(written) && written == data.size();
    }

    bool CompressedFile::ReadHead(const std::filesystem::path& path, size_t size, std::vector<uint8_t>& data)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        std::vector<uint8_t> chunk(std::max(size, kHeadReadChunk));
        file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(sizeof(kFrameMagic)));
        size_t filled = static_cast<size_t>(file.gcount());
        if (!StartsWithMagic(chunk.data(), filled)) {
            file.read(reinterpret_cast<char*>(chunk.data() + filled), static_cast<std::streamsize>(size - std::min(size, filled)));
            filled += static_cast<size_t>(file.gcount());
            data.assign(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(std::min(size, filled)));
            return true;
        }

        // 圧縮してあれば、出力が size に達するまで少しずつ読んで展開する
        ZSTD_DCtx* context = DecompressContext();
        data.resize(size);
        ZSTD_outBuffer output{ data.data(), data.size(), 0 };
        while (output.pos < output.size) {
            file.read(reinterpret_cast<char*>(chunk.data() + filled), static_cast<std::streamsize>(kHeadReadChunk - filled));
            filled += static_cast<size_t>(file.gcount());
            if (filled == 0) {
                break;
            }
            ZSTD_inBuffer input{ chunk.data(), filled, 0 };
            while (input.pos < input.size && output.pos < output.size) {
                const size_t result = ZSTD_decompressStream(context, &output, &input);
                if (ZSTD_isError(result)) {
                    return false;
                }
                if (result == 0) {
                    break;
                }
            }
            if (input.pos < input.size && output.pos < output.size) {
                break;
            }
            filled = 0;
        }
        data.resize(output.pos);
        return true;
    }

    bool CompressedFile::WriteCompressed(const std::filesystem::path& source, const std::filesystem::path& destination)
    {
        std::vector<uint8_t> raw;
        if (!ReadRaw(source, raw)) {
            return false;
        }
        std::vector<uint8_t> packed(ZSTD_compressBound(raw.size()));
        const size_t written = ZSTD_compressCCtx(CompressContext(), packed.data(), packed.size(), raw.data(), raw.size(),
            kCompressionLevel);
        if (ZSTD_isError(written)) {
            return false;
        }

        std::ofstream file(destination, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(packed.data()), static_cast<std::streamsize>(written));
        return static_cast<bool>(file);
    }
}
