#pragma once

#include <filesystem>

#include "Graphics/Texture/TextureColorSpace.h"

namespace CoreEngine
{
    /// @brief 書き出し時に画像から作る DDS（クック済みテクスチャ）の名前
    /// @details `Application/Assets/a/b.png` の DDS は `Application/Cooked/a/b.png.dds`、
    ///          リニア版は `Application/Cooked/a/b.png.linear.dds` に置く（置き場は CookedPath）。
    ///          書き出したゲームは元の画像を持たず、この DDS を読む。
    namespace CookedTexture
    {
        /// @brief 書き出し時に DDS へ変換する画像か（拡張子で判定）
        bool IsCookable(const std::filesystem::path& path);

        /// @brief 元の画像の綴りから、クック済みの DDS の綴りを作る
        /// @param sourceRelative `Application/Assets/…` か `Engine/Assets/…` の綴り
        /// @return 変換の対象でなければ空
        std::filesystem::path ToCookedPath(const std::filesystem::path& sourceRelative, TextureColorSpace colorSpace);

        /// @brief クック済みの DDS の綴りから、元の画像の綴りを作る（ToCookedPath の逆）
        /// @param cookedRelative `Application/Cooked/…` か `Engine/Cooked/…` の綴り
        /// @return クック済みの DDS の綴りでなければ空
        std::filesystem::path ToSourcePath(const std::filesystem::path& cookedRelative);

        /// @brief 元の画像に対応するクック済みの DDS を探す
        /// @param sourcePath 元の画像の絶対パス（ファイルが無くてもよい）
        /// @return 見つかった DDS の絶対パス。無ければ空
        std::filesystem::path Find(const std::filesystem::path& sourcePath, TextureColorSpace colorSpace);
    }
}
