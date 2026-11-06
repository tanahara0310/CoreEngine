#pragma once

#include <string_view>

namespace CoreEngine
{
    /// @brief テクスチャの色空間指定
    /// @note PBR ではベースカラー/エミッシブのみ sRGB、
    ///       法線・MetallicRoughness・AO などのデータテクスチャはリニア（法線は Normal）で読み込む必要がある。
    ///       sRGB として読むとサンプリング時にガンマデコードされ、データ値が歪む。
    enum class TextureColorSpace {
        SRGB,   ///< カラー画像（ベースカラー・エミッシブ・UIなど）。ガンマデコードあり
        Linear, ///< データ画像（MetallicRoughness・AO・マスクなど）。補正なし
        Normal, ///< 法線マップ。補正なしで、既定の圧縮は赤と緑だけの BC5
    };

    /// @brief ガンマデコードせずに読むか
    inline bool IsLinearData(TextureColorSpace colorSpace)
    {
        return colorSpace != TextureColorSpace::SRGB;
    }

    /// @brief 色空間を取り込み設定の文字列にする（"srgb" / "linear" / "normal"）
    inline const char* ColorSpaceName(TextureColorSpace colorSpace)
    {
        switch (colorSpace) {
        case TextureColorSpace::Linear: return "linear";
        case TextureColorSpace::Normal: return "normal";
        default: return "srgb";
        }
    }

    /// @brief 画像の取り込み設定（.meta の import.colorSpace）があれば、頼まれた色空間より優先する
    /// @param importSetting "srgb" / "linear" / "normal" / 空（設定なし）
    /// @details "linear" は法線マップとして頼まれた画像を法線マップのまま読む
    inline TextureColorSpace ApplyImportColorSpace(std::string_view importSetting, TextureColorSpace requested)
    {
        if (importSetting == "normal") {
            return TextureColorSpace::Normal;
        }
        if (importSetting == "linear") {
            return (requested == TextureColorSpace::Normal) ? TextureColorSpace::Normal : TextureColorSpace::Linear;
        }
        if (importSetting == "srgb") {
            return TextureColorSpace::SRGB;
        }
        return requested;
    }
}
