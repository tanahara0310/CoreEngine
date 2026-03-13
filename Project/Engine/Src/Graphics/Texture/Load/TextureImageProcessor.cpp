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

    HRESULT TextureImageProcessor::LoadTextureImage(const std::wstring& filePath, DirectX::ScratchImage& image)
    {
        // 拡張子判定結果に応じて最適なDirectXTexローダーを呼び分ける。
        FileType fileType = DetectFileType(filePath);
        if (fileType == FileType::DDS) {
            return DirectX::LoadFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
        }
        if (fileType == FileType::HDR) {
            return DirectX::LoadFromHDRFile(filePath.c_str(), nullptr, image);
        }

        return DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
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

        return DirectX::GenerateMipMaps(
            image.GetImages(),
            image.GetImageCount(),
            image.GetMetadata(),
            DirectX::TEX_FILTER_SRGB,
            0,
            mipImages
        );
    }
}
