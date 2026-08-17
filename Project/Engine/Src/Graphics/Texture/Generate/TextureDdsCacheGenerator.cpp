#include "pch.h"
#include "TextureDdsCacheGenerator.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <externals/DirectXTex/DirectXTex.h>
#include <filesystem>
#include <format>
#include <exception>

namespace CoreEngine
{
    bool TextureDdsCacheGenerator::GenerateCache(const std::filesystem::path& sourcePath,
        const std::filesystem::path& ddsPath, TextureColorSpace colorSpace) const
    {
        Logger& log = Logger::GetInstance();

        // ログに出すのは表示用の UTF-8 文字列。ファイルI/Oには path をそのまま使う。
        const std::string sourceForDisplay = log.PathToUtf8(sourcePath);
        const std::string ddsForDisplay = log.PathToUtf8(ddsPath);

        try {
            // 入力画像を読み込み、形式に応じたローダーはImageProcessorへ委譲する。
            DirectX::ScratchImage sourceImage;
            HRESULT hr = TextureImageProcessor::LoadTextureImage(sourcePath.wstring(), sourceImage, colorSpace);

            if (FAILED(hr)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "Failed to load source for DDS generation: {}", sourceForDisplay);
                return false;
            }

            // サンプリング品質のためにミップチェーンを構築し、失敗時は元画像へフォールバックする。
            DirectX::ScratchImage mipChain;
            hr = TextureImageProcessor::BuildMipChain(sourceImage, mipChain);
            if (FAILED(hr)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "Failed to generate mipmaps for DDS: {}", sourceForDisplay);
                mipChain = std::move(sourceImage);
            }

            // DDS容量削減のためBC3に圧縮し、失敗時は非圧縮DDSとして保存する。
            // 色空間に合わせて圧縮フォーマットを切り替える（Linear データを SRGB で保存すると値が歪む）。
            const DXGI_FORMAT compressFormat = (colorSpace == TextureColorSpace::Linear)
                ? DXGI_FORMAT_BC3_UNORM
                : DXGI_FORMAT_BC3_UNORM_SRGB;
            DirectX::ScratchImage compressedImage;
            hr = DirectX::Compress(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                compressFormat,
                DirectX::TEX_COMPRESS_PARALLEL,
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressedImage);

            DirectX::ScratchImage* imageToSave = &mipChain;
            if (SUCCEEDED(hr)) {
                imageToSave = &compressedImage;
            } else {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "Failed to compress texture, saving uncompressed DDS: {}", sourceForDisplay);
            }

            // 最終的にDDSとして保存し、結果をログに出力する。
            hr = DirectX::SaveToDDSFile(
                imageToSave->GetImages(),
                imageToSave->GetImageCount(),
                imageToSave->GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                ddsPath.wstring().c_str());

            if (SUCCEEDED(hr)) {
                log.Logf(LogLevel::INFO, LogCategory::Resource, "  DDS cache generated: {}", ddsForDisplay);
                return true;
            }

            log.Logf(LogLevel::WARNING, LogCategory::Resource, "Failed to save DDS cache: {}", ddsForDisplay);
            return false;
        }
        catch (const std::exception& e) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "Exception during DDS generation: {}", e.what());
            return false;
        }
    }
}
