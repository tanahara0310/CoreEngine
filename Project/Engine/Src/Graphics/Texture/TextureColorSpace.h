#pragma once

#include <string_view>

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

    /// @brief 画像の取り込み設定（.meta の import.colorSpace）があれば、頼まれた色空間より優先する
    /// @param importSetting "srgb" / "linear" / 空（設定なし）
    inline TextureColorSpace ApplyImportColorSpace(std::string_view importSetting, TextureColorSpace requested)
    {
        if (importSetting == "linear") {
            return TextureColorSpace::Linear;
        }
        if (importSetting == "srgb") {
            return TextureColorSpace::SRGB;
        }
        return requested;
    }
}
