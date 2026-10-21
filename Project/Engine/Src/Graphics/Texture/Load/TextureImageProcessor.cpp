#include "pch.h"
#include "TextureImageProcessor.h"

#include <filesystem>
#include <algorithm>
#include <cwctype>

namespace CoreEngine
{
    TextureImageProcessor::FileType TextureImageProcessor::DetectFileType(const std::wstring& filePath)
    {
        std::wstring extension = std::filesystem::path(filePath).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);

        if (extension == L".dds") {
            return FileType::DDS;
        }
        if (extension == L".hdr") {
            return FileType::HDR;
        }

        return FileType::WIC;
    }

    HRESULT TextureImageProcessor::LoadTextureImage(const std::wstring& filePath, DirectX::ScratchImage& image,
        TextureColorSpace colorSpace)
    {
        // 拡張子判定結果に応じて最適なDirectXTexローダーを呼び分ける。
        FileType fileType = DetectFileType(filePath);
        if (fileType == FileType::DDS) {
            return DirectX::LoadFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        }
        if (fileType == FileType::HDR) {
            return DirectX::LoadFromHDRFile(filePath.c_str(), nullptr, image);
        }

        // Linear は埋め込みの色情報を無視して UNORM（非 sRGB）の形式で読む。
        if (colorSpace == TextureColorSpace::Linear) {
            return DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, image);
        }

        // SRGB は色情報の無い画像も sRGB として読み、色情報を持てない形式（BMP・TGA など）は
        // 読んだ後に sRGB の形式へ付け替える。
        const HRESULT hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_DEFAULT_SRGB, nullptr, image);
        if (SUCCEEDED(hr)) {
            image.OverrideFormat(DirectX::MakeSRGB(image.GetMetadata().format));
        }
        return hr;
    }

    HRESULT TextureImageProcessor::LoadMetadata(const std::wstring& filePath, DirectX::TexMetadata& metadata)
    {
        FileType fileType = DetectFileType(filePath);
        if (fileType == FileType::DDS) {
            return DirectX::GetMetadataFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, metadata);
        }
        if (fileType == FileType::HDR) {
            return DirectX::GetMetadataFromHDRFile(filePath.c_str(), metadata);
        }

        const HRESULT hr = DirectX::GetMetadataFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_DEFAULT_SRGB, metadata);
        if (SUCCEEDED(hr)) {
            metadata.format = DirectX::MakeSRGB(metadata.format);
        }
        return hr;
    }

    HRESULT TextureImageProcessor::BuildMipChain(DirectX::ScratchImage& image, DirectX::ScratchImage& mipImages)
    {
        if (DirectX::IsCompressed(image.GetMetadata().format)) {
            mipImages = std::move(image);
            return S_OK;
        }

        const DirectX::TexMetadata& metadata = image.GetMetadata();
        if (metadata.width == 1 && metadata.height == 1) {
            mipImages = std::move(image);
            return S_OK;
        }

        // sRGBフォーマットのみガンマ補正フィルタを適用する。
        // HDR / float テクスチャ（R16F, R32F 等）はリニア空間なので
        // TEX_FILTER_SRGB を適用するとミップ生成時に pow(x,2.2) が掛かり、
        // HDR の輝度値（>1.0）が爆発的に増大して白飛びの原因になる。
        DirectX::TEX_FILTER_FLAGS filterFlags = DirectX::TEX_FILTER_LINEAR;
        if (DirectX::IsSRGB(metadata.format)) {
            filterFlags = static_cast<DirectX::TEX_FILTER_FLAGS>(filterFlags | DirectX::TEX_FILTER_SRGB);
        }

        return DirectX::GenerateMipMaps(
            image.GetImages(),
            image.GetImageCount(),
            image.GetMetadata(),
            filterFlags,
            0,
            mipImages
        );
    }
}
