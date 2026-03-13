#pragma once

#include <string>

namespace CoreEngine
{
    /// @brief 通常テクスチャからDDSキャッシュを生成する責務を持つクラス
    class TextureDdsCacheGenerator
    {
    public:
        /// @brief ソース画像を読み込み、必要なミップ生成・圧縮を経てDDSとして保存する
        /// @param sourcePath 変換元テクスチャパス
        /// @param ddsPath 出力DDSパス
        /// @return 保存成功時true
        bool GenerateCache(const std::string& sourcePath, const std::string& ddsPath) const;
    };
}
