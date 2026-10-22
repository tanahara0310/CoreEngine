#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "Graphics/Texture/TextureColorSpace.h"

namespace CoreEngine
{
    /// @brief 画像の圧縮の形式（.meta の import.format）
    enum class TextureCompression
    {
        BC7,  ///< BC7（モード 6 だけを使う速い圧縮）
        BC3,  ///< BC3（DXT5）
        None, ///< 圧縮しない（RGBA 各 8 ビット）
    };

    /// @brief 画像（PNG・JPG など）を、GPU がそのまま読める DDS（ミップ付き・BC 圧縮）へ変換する
    /// @details 開発中の派生データと書き出しのクックの両方が使う。GPU もキャッシュも触らない。
    namespace TextureImporter
    {
        /// @brief 変換の中身を変えたら上げる版（派生データのキーに入り、古い DDS を作り直させる）
        inline constexpr uint32_t kVersion = 1;

        /// @brief 取り込み設定で形式を決めていない画像の形式
        inline constexpr TextureCompression kDefaultCompression = TextureCompression::BC3;

        /// @brief 取り込み設定の文字列（"bc7" / "bc3" / "none" / 空）から形式を決める（空なら既定）
        TextureCompression ParseCompression(std::string_view setting);

        /// @brief 形式を取り込み設定の文字列にする
        const char* CompressionName(TextureCompression compression);

        /// @brief 変換の設定の文字列（派生データのキーに入る）
        std::string Settings(TextureColorSpace colorSpace, TextureCompression compression);

        /// @brief 画像を読み、ミップを作って圧縮し、DDS として書く
        /// @param sourcePath 元の画像
        /// @param ddsPath 書き出す DDS
        /// @param colorSpace SRGB は *_UNORM_SRGB、Linear は *_UNORM の形式で書く
        /// @param compression 圧縮の形式
        /// @return 書けたら true
        bool Import(const std::filesystem::path& sourcePath, const std::filesystem::path& ddsPath,
            TextureColorSpace colorSpace, TextureCompression compression);
    }
}
