#pragma once

#include <string>

#include "Graphics/Texture/TextureColorSpace.h"

namespace CoreEngine
{
    /// @brief 通常テクスチャからDDSキャッシュを生成する責務を持つクラス
    class TextureDdsCacheGenerator
    {
    public:
        /// @brief ソース画像を読み込み、必要なミップ生成・圧縮を経てDDSとして保存する
        /// @param sourcePath 変換元テクスチャパス
        /// @param ddsPath 出力DDSパス
        /// @param colorSpace 色空間（SRGB=BC3_UNORM_SRGB / Linear=BC3_UNORM で圧縮）
        /// @return 保存成功時true
        bool GenerateCache(const std::string& sourcePath, const std::string& ddsPath,
            TextureColorSpace colorSpace = TextureColorSpace::SRGB) const;
    };
}
