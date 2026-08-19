#pragma once

#include "Graphics/Texture/Gpu/TextureGpuUploader.h"
#include "Graphics/Texture/TextureColorSpace.h"
#include <externals/DirectXTex/DirectXTex.h>

#include <filesystem>
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
        /// @param colorSpace 色空間（WIC 読み込み時の sRGB / Linear 切り替え）
        static ExecutionResult Execute(
            CoreEngine::DirectXCommon* dxCommon,
            const std::filesystem::path& resolvedPath,
            bool ddsGenerationEnabled,
            const std::filesystem::path& ddsPath,
            const std::function<bool(const std::filesystem::path&, const std::filesystem::path&)>& ddsCacheGenerator,
            TextureColorSpace colorSpace = TextureColorSpace::SRGB);
    };
}
