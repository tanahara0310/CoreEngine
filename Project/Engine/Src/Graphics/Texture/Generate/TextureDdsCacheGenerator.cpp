#include "TextureDdsCacheGenerator.h"
#include "Graphics/Texture/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <externals/DirectXTex/DirectXTex.h>
#include <format>
#include <exception>

namespace CoreEngine
{
    bool TextureDdsCacheGenerator::GenerateCache(const std::string& sourcePath, const std::string& ddsPath) const
    {
        try {
            // 入力画像を読み込み、形式に応じたローダーはImageProcessorへ委譲する。
            DirectX::ScratchImage sourceImage;
            std::wstring sourcePathW = Logger::GetInstance().ConvertString(sourcePath);
            HRESULT hr = TextureImageProcessor::LoadTextureImage(sourcePathW, sourceImage);

            if (FAILED(hr)) {
                Logger::GetInstance().Log(std::format("Failed to load source for DDS generation: {}", sourcePath), LogLevel::WARNING, LogCategory::Graphics);
                return false;
            }

            // サンプリング品質のためにミップチェーンを構築し、失敗時は元画像へフォールバックする。
            DirectX::ScratchImage mipChain;
            hr = TextureImageProcessor::BuildMipChain(sourceImage, mipChain);
            if (FAILED(hr)) {
                Logger::GetInstance().Log(std::format("Failed to generate mipmaps for DDS: {}", sourcePath), LogLevel::WARNING, LogCategory::Graphics);
                mipChain = std::move(sourceImage);
            }

            // DDS容量削減のためBC3に圧縮し、失敗時は非圧縮DDSとして保存する。
            DirectX::ScratchImage compressedImage;
            hr = DirectX::Compress(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                DXGI_FORMAT_BC3_UNORM_SRGB,
                DirectX::TEX_COMPRESS_PARALLEL,
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressedImage);

            DirectX::ScratchImage* imageToSave = &mipChain;
            if (SUCCEEDED(hr)) {
                imageToSave = &compressedImage;
            } else {
                Logger::GetInstance().Log(std::format("Failed to compress texture, saving uncompressed DDS: {}", sourcePath), LogLevel::WARNING, LogCategory::Graphics);
            }

            // 最終的にDDSとして保存し、結果をログに出力する。
            std::wstring ddsPathW = Logger::GetInstance().ConvertString(ddsPath);
            hr = DirectX::SaveToDDSFile(
                imageToSave->GetImages(),
                imageToSave->GetImageCount(),
                imageToSave->GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                ddsPathW.c_str());

            if (SUCCEEDED(hr)) {
                Logger::GetInstance().Log(std::format("DDS cache generated: {}", ddsPath), LogLevel::INFO, LogCategory::Graphics);
                return true;
            }

            Logger::GetInstance().Log(std::format("Failed to save DDS cache: {}", ddsPath), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
        catch (const std::exception& e) {
            Logger::GetInstance().Log(std::format("Exception during DDS generation: {}", e.what()), LogLevel::WARNING, LogCategory::Graphics);
            return false;
        }
    }
}
