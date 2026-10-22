#pragma once

#include <filesystem>
#include <string>

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

        /// @brief 変換の設定の文字列（色空間と、取り込み設定か既定の圧縮の形式）
        std::string Settings(const std::filesystem::path& source, TextureColorSpace colorSpace);

        /// @brief 古い置き場（`<プロジェクト>/Intermediate/TextureCache`）に残った画像の DDS を消す
        /// @details HDR のキューブマップ（名前に `_cubemap` を含む）は今もそこに置くので残す。テクスチャを読み込む前に呼ぶ。
        void RemoveLegacyCache();
    }
}
