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

        // 色空間指定に応じて WIC フラグを切り替える。
        // 法線・MetallicRoughness・AO などのデータテクスチャを sRGB として読み込むと
        // サンプリング時にガンマデコードが掛かり値が歪むため、Linear では
        // 埋め込みプロファイルを無視して UNORM（非sRGB）フォーマットで読み込む。
        const DirectX::WIC_FLAGS wicFlags = (colorSpace == TextureColorSpace::Linear)
            ? DirectX::WIC_FLAGS_IGNORE_SRGB
            : DirectX::WIC_FLAGS_FORCE_SRGB;
        return DirectX::LoadFromWICFile(filePath.c_str(), wicFlags, nullptr, image);
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

        return DirectX::GetMetadataFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, metadata);
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
