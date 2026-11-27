#include "pch.h"
#include "TextureImporter.h"
#include "Graphics/Texture/Import/TextureBlockEncoder.h"
#include "Graphics/Texture/Load/TextureImageProcessor.h"
#include "Utility/Enum/EnumNames.h"
#include "Utility/Logger/Logger.h"

#include <externals/DirectXTex/DirectXTex.h>
#include <exception>
#include <filesystem>

namespace CoreEngine
{
    namespace
    {
        /// @brief 圧縮の形式と取り込み設定の文字列の表
        constexpr EnumName<TextureCompression> kCompressionNames[] = {
            { TextureCompression::Auto, "auto" },
            { TextureCompression::BC7,  "bc7" },
            { TextureCompression::BC3,  "bc3" },
            { TextureCompression::BC1,  "bc1" },
            { TextureCompression::BC5,  "bc5" },
            { TextureCompression::None, "none" },
        };

        /// @brief 圧縮の形式に合う DXGI の形式（BC5 は常に BC5_UNORM）
        DXGI_FORMAT CompressedFormat(TextureCompression compression, bool linear)
        {
            switch (compression) {
            case TextureCompression::BC1: return linear ? DXGI_FORMAT_BC1_UNORM : DXGI_FORMAT_BC1_UNORM_SRGB;
            case TextureCompression::BC3: return linear ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC3_UNORM_SRGB;
            case TextureCompression::BC5: return DXGI_FORMAT_BC5_UNORM;
            default: break;
            }
            return linear ? DXGI_FORMAT_BC7_UNORM : DXGI_FORMAT_BC7_UNORM_SRGB;
        }

        /// @brief ミップの画像を全部圧縮する（BC5 は DirectXTex、BC1・BC3・BC7 は RGBA 各 8 ビットへ直してから TextureBlockEncoder）
        HRESULT CompressImages(const DirectX::ScratchImage& source, TextureCompression compression, bool linear,
            DirectX::ScratchImage& result)
        {
            const DXGI_FORMAT compressedFormat = CompressedFormat(compression, linear);
            if (compression == TextureCompression::BC5) {
                return DirectX::Compress(source.GetImages(), source.GetImageCount(), source.GetMetadata(), compressedFormat,
                    DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, result);
            }
            TextureBlockEncoder::Format format = TextureBlockEncoder::Format::BC7;
            if (compression == TextureCompression::BC1) {
                format = TextureBlockEncoder::Format::BC1;
            } else if (compression == TextureCompression::BC3) {
                format = TextureBlockEncoder::Format::BC3;
            }

            // 圧縮先が sRGB の形式なら sRGB のまま、そうでなければリニアの値の 8 ビットで画素を並べる
            const DXGI_FORMAT rgbaFormat = DirectX::IsSRGB(compressedFormat) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            const DirectX::ScratchImage* rgba = &source;
            DirectX::ScratchImage converted;
            if (source.GetMetadata().format != rgbaFormat) {
                const HRESULT hr = DirectX::Convert(source.GetImages(), source.GetImageCount(), source.GetMetadata(), rgbaFormat,
                    DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_FORCE_NON_WIC, DirectX::TEX_THRESHOLD_DEFAULT, converted);
                if (FAILED(hr)) {
                    return hr;
                }
                rgba = &converted;
            }

            DirectX::TexMetadata metadata = rgba->GetMetadata();
            metadata.format = compressedFormat;
            const HRESULT hr = result.Initialize(metadata);
            if (FAILED(hr)) {
                return hr;
            }
            for (size_t i = 0; i < rgba->GetImageCount(); ++i) {
                const DirectX::Image& from = rgba->GetImages()[i];
                const DirectX::Image& to = result.GetImages()[i];
                TextureBlockEncoder::Encode(from.pixels, from.rowPitch, static_cast<uint32_t>(from.width), static_cast<uint32_t>(from.height),
                    format, to.pixels, to.rowPitch);
            }
            return S_OK;
        }
    }

    TextureCompression TextureImporter::ParseCompression(std::string_view setting)
    {
        return ParseName(kCompressionNames, setting, kDefaultCompression);
    }

    const char* TextureImporter::CompressionName(TextureCompression compression)
    {
        return NameOf(kCompressionNames, compression, "auto").data();
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
            // 圧縮できない画像（8 ビットへ直せない画素の形式など）は圧縮せずに書く
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
                hr = CompressImages(mipChain, compression, IsLinearData(colorSpace), compressedImage);
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
