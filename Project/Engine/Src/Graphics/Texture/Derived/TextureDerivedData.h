#pragma once

#include <filesystem>

#include "Graphics/Asset/Derived/DerivedData.h"
#include "Graphics/Texture/TextureColorSpace.h"

namespace CoreEngine
{
    /// @brief 画像（PNG・JPG など）の派生データ（DDS）を探す・作る
    /// @details クック済みは `Application/Cooked/a/b.png.dds`（リニア版は `.linear.dds`）、
    ///          開発中は `<プロジェクト>/Intermediate/DerivedData/Texture/<GUID>.srgb.dds`（リニア版は `.linear.dds`）。
    ///          GUID の無い画像は、GUID の代わりに綴りのハッシュを名前にする。
    namespace TextureDerivedData
    {
        /// @brief 開発中の置き場
        std::filesystem::path IntermediatePath(const std::filesystem::path& source, TextureColorSpace colorSpace);

        /// @brief 元の画像に対応する DDS を探す（クック済み → 開発中の置き場 → いま変換）
        /// @param source 元の画像の絶対パス（書き出したゲームではファイルが無くてもよい）
        DerivedData::Result Find(const std::filesystem::path& source, TextureColorSpace colorSpace);

        /// @brief 開発中の置き場の DDS を新しくして返す（書き出しのクックが使う）
        DerivedData::Result EnsureIntermediate(const std::filesystem::path& source, TextureColorSpace colorSpace);
    }
}
