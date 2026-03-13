#pragma once

#include <externals/DirectXTex/DirectXTex.h>
#include <string>

namespace CoreEngine
{
    /// @brief テクスチャメタデータのロードとエラー処理を担当するクラス
    class TextureMetadataLoader
    {
    public:
        /// @brief 指定パスのメタデータをロードし、失敗時はログとダイアログを表示して例外送出する
        /// @param resolvedPath 解決済みテクスチャパス
        /// @return ロード済みメタデータ
        static DirectX::TexMetadata LoadOrThrow(const std::string& resolvedPath);
    };
}
