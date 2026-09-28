#include "pch.h"
#include "Editor/Launcher/ProjectThumbnails.h"

#ifdef CORE_EDITOR

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Texture/Gpu/TextureGpuUploader.h"
#include "externals/DirectXTex/DirectXTex.h"

#include <wincodec.h>

#include <cmath>
#include <exception>

namespace CoreEngine::Editor
{
    namespace
    {
        /// サムネイルの幅（高さは縦横比から決める）
        constexpr size_t kThumbnailWidth = 480;

        /// @brief path を表示用の UTF-8 にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }
    }

    std::filesystem::path ProjectThumbnails::FilePath(const std::filesystem::path& projectFolder)
    {
        return projectFolder / "Application" / "Saved" / "Thumbnail.png";
    }

    bool ProjectThumbnails::Capture(GraphicsCore& graphics, ID3D12Resource* source, D3D12_RESOURCE_STATES state,
                                    float aspect, const std::filesystem::path& file)
    {
        if (!source || aspect <= 0.0f) {
            return false;
        }

        DirectX::ScratchImage captured;
        if (FAILED(DirectX::CaptureTexture(graphics.GetCommandQueue(), source, false, captured, state, state))) {
            return false;
        }

        // 縦横比を保って縮める
        const size_t height = static_cast<size_t>(std::lround(static_cast<float>(kThumbnailWidth) / aspect));
        DirectX::ScratchImage resized;
        if (FAILED(DirectX::Resize(*captured.GetImage(0, 0, 0), kThumbnailWidth, height,
                                   DirectX::TEX_FILTER_DEFAULT, resized))) {
            return false;
        }

        // リニアの色を sRGB の 8 ビットにする
        DirectX::ScratchImage converted;
        if (FAILED(DirectX::Convert(*resized.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                                    DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
            return false;
        }

        // アルファを落とした PNG で保存する
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        return SUCCEEDED(DirectX::SaveToWICFile(*converted.GetImage(0, 0, 0), DirectX::WIC_FLAGS_FORCE_SRGB,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), file.c_str(), &GUID_WICPixelFormat24bppBGR));
    }

    ProjectThumbnails::ProjectThumbnails(GraphicsCore& graphics)
        : graphics_(graphics)
    {
    }

    ImTextureID ProjectThumbnails::Get(const std::filesystem::path& projectFolder)
    {
        const std::filesystem::path file = FilePath(projectFolder).lexically_normal();
        const auto found = entries_.find(file.native());
        if (found != entries_.end()) {
            return found->second.id;
        }

        // 読めなかったものも覚えて、次からは読み直さない
        Entry& entry = entries_[file.native()];
        std::error_code ec;
        if (!std::filesystem::is_regular_file(file, ec)) {
            return entry.id;
        }

        DirectX::ScratchImage image;
        if (FAILED(DirectX::LoadFromWICFile(file.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image))) {
            return entry.id;
        }
        DirectX::ScratchImage mipChain;
        const bool mipped = SUCCEEDED(DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(),
            image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipChain));

        try {
            const TextureGpuUploader::UploadResult uploaded =
                TextureGpuUploader::UploadAndCreateSrv(&graphics_, mipped ? mipChain : image, ToUtf8(file));
            entry.texture = uploaded.texture;
            entry.id = static_cast<ImTextureID>(uploaded.descriptor.gpuHandle.ptr);
        }
        catch (const std::exception&) {
            entry = {};
        }
        return entry.id;
    }
}

#endif // CORE_EDITOR
