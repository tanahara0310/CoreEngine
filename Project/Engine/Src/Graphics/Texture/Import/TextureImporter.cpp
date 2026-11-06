#include "pch.h"
#include "TextureImporter.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Logger/Logger.h"

#include <externals/DirectXTex/DirectXTex.h>
#include <exception>
#include <filesystem>

namespace CoreEngine
{
    TextureCompression TextureImporter::ParseCompression(std::string_view setting)
    {
        if (setting == "bc7") {
            return TextureCompression::BC7;
        }
        if (setting == "bc3") {
            return TextureCompression::BC3;
        }
        if (setting == "bc1") {
            return TextureCompression::BC1;
        }
        if (setting == "bc5") {
            return TextureCompression::BC5;
        }
        if (setting == "none") {
            return TextureCompression::None;
        }
        return kDefaultCompression;
    }

    const char* TextureImporter::CompressionName(TextureCompression compression)
    {
        switch (compression) {
        case TextureCompression::Auto: return "auto";
        case TextureCompression::BC3: return "bc3";
        case TextureCompression::BC1: return "bc1";
        case TextureCompression::BC5: return "bc5";
        case TextureCompression::None: return "none";
        default: return "bc7";
        }
    }

    std::string TextureImporter::Settings(TextureColorSpace colorSpace, TextureCompression compression)
    {
        return std::string(ColorSpaceName(colorSpace)) + ";" + CompressionName(compression);
    }

    bool TextureImporter::Import(const std::filesystem::path& sourcePath, const std::filesystem::path& ddsPath,
        TextureColorSpace colorSpace, TextureCompression compression)
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

            // 形式と色空間に合わせて圧縮する。1 枚の圧縮は 1 スレッドで行い、画像ごとの並列は呼ぶ側のスレッドプールに任せる。
            // 圧縮できない大きさ（4x4 未満など）は非圧縮のまま書く
            // 自動は、法線マップなら赤と緑だけの BC5、全画素が不透明なら透明を持たない BC1、そうでなければ BC3 にする
            if (compression == TextureCompression::Auto) {
                if (colorSpace == TextureColorSpace::Normal) {
                    compression = TextureCompression::BC5;
                } else {
                    compression = mipChain.IsAlphaAllOpaque() ? TextureCompression::BC1 : TextureCompression::BC3;
                }
            }
            DirectX::ScratchImage compressedImage;
            hr = E_FAIL;
            if (compression != TextureCompression::None) {
                const bool linear = IsLinearData(colorSpace);
                DXGI_FORMAT compressFormat = linear ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC7_UNORM_SRGB;
                DirectX::TEX_COMPRESS_FLAGS flags = DirectX::TEX_COMPRESS_BC7_QUICK;
                if (compression == TextureCompression::BC3) {
                    compressFormat = linear ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC3_UNORM_SRGB;
                    flags = DirectX::TEX_COMPRESS_DEFAULT;
                } else if (compression == TextureCompression::BC1) {
                    compressFormat = linear ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC1_UNORM_SRGB;
                    flags = DirectX::TEX_COMPRESS_DEFAULT;
                } else if (compression == TextureCompression::BC5) {
                    compressFormat = DXGI_FORMAT_BC5_UNORM;
                    flags = DirectX::TEX_COMPRESS_DEFAULT;
                }
                hr = DirectX::Compress(
                    mipChain.GetImages(),
                    mipChain.GetImageCount(),
                    mipChain.GetMetadata(),
                    compressFormat,
                    flags,
                    DirectX::TEX_THRESHOLD_DEFAULT,
                    compressedImage);
                if (FAILED(hr)) {
                    log.Logf(LogLevel::WARNING, LogCategory::Resource, "圧縮できないので非圧縮の DDS にします: {}", sourceForDisplay);
                }
            }
            const DirectX::ScratchImage& imageToSave = SUCCEEDED(hr) ? compressedImage : mipChain;

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
