#include "pch.h"
#include "TextureImporter.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <externals/DirectXTex/DirectXTex.h>
#include <exception>
#include <filesystem>

namespace CoreEngine
{
    std::string TextureImporter::Settings(TextureColorSpace colorSpace)
    {
        return (colorSpace == TextureColorSpace::Linear) ? "linear;bc3" : "srgb;bc3";
    }

    bool TextureImporter::Import(const std::filesystem::path& sourcePath, const std::filesystem::path& ddsPath,
        TextureColorSpace colorSpace)
    {
        Logger& log = Logger::GetInstance();
        const std::string sourceForDisplay = log.PathToUtf8(sourcePath);

        try {
            DirectX::ScratchImage sourceImage;
            HRESULT hr = TextureImageProcessor::LoadTextureImage(sourcePath.wstring(), sourceImage, colorSpace);
            if (FAILED(hr)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "画像を読めないので DDS にできません: {}", sourceForDisplay);
                return false;
            }

            // ミップを作れなければ 1 段のまま圧縮する
            DirectX::ScratchImage mipChain;
            hr = TextureImageProcessor::BuildMipChain(sourceImage, mipChain);
            if (FAILED(hr)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "ミップを作れないので 1 段で変換します: {}", sourceForDisplay);
                mipChain = std::move(sourceImage);
            }

            // 色空間に合わせて BC3 の形式を選ぶ。1 枚の圧縮は 1 スレッドで行い、画像ごとの並列は読み込みのスレッドプールに任せる。
            // 圧縮できない大きさ（4x4 未満など）は非圧縮のまま書く
            const DXGI_FORMAT compressFormat = (colorSpace == TextureColorSpace::Linear)
                ? DXGI_FORMAT_BC3_UNORM
                : DXGI_FORMAT_BC3_UNORM_SRGB;
            DirectX::ScratchImage compressedImage;
            hr = DirectX::Compress(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                compressFormat,
                DirectX::TEX_COMPRESS_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                compressedImage);

            const DirectX::ScratchImage& imageToSave = SUCCEEDED(hr) ? compressedImage : mipChain;
            if (FAILED(hr)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "圧縮できないので非圧縮の DDS にします: {}", sourceForDisplay);
            }

            hr = DirectX::SaveToDDSFile(
                imageToSave.GetImages(),
                imageToSave.GetImageCount(),
                imageToSave.GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                ddsPath.wstring().c_str());
            if (FAILED(hr)) {
                log.Logf(LogLevel::WARNING, LogCategory::Resource, "DDS を書けません: {}", log.PathToUtf8(ddsPath));
                return false;
            }
            return true;
        }
        catch (const std::exception& e) {
            log.Logf(LogLevel::WARNING, LogCategory::Resource, "DDS への変換中に例外: {} ({})", sourceForDisplay, e.what());
            return false;
        }
    }
}
