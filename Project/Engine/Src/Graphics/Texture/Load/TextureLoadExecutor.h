#pragma once

#include "Graphics/Texture/Gpu/TextureGpuUploader.h"
#include <externals/DirectXTex/DirectXTex.h>

#include <string>
#include <functional>

namespace CoreEngine
{
    class DirectXCommon;

    /// @brief テクスチャ読み込みの実行処理（デコード・ミップ生成・GPU転送）を担当するクラス
    class TextureLoadExecutor
    {
    public:
        /// @brief 読み込み実行後に呼び出し元へ返す結果
        struct ExecutionResult
        {
            TextureGpuUploader::UploadResult uploadResult;
            DirectX::TexMetadata metadata{};
        };

        /// @brief 読み込み計画に基づいてテクスチャロードを実行する
        /// @param dxCommon DirectX共通管理
        /// @param resolvedPath 実際に読み込むファイルパス
        /// @param isDDS DDS読み込みフラグ
        /// @param isHDR HDR読み込みフラグ
        /// @param ddsGenerationEnabled DDS生成有効フラグ
        /// @param ddsPath DDS生成先パス
        /// @param ddsCacheGenerator DDS生成処理コールバック
        /// @return アップロード結果とメタデータ
        static ExecutionResult Execute(
            CoreEngine::DirectXCommon* dxCommon,
            const std::string& resolvedPath,
            bool isDDS,
            bool isHDR,
            bool ddsGenerationEnabled,
            const std::string& ddsPath,
            const std::function<bool(const std::string&, const std::string&)>& ddsCacheGenerator);
    };
}
