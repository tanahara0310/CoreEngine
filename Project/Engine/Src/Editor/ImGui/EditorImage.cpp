#include "pch.h"
#include "Editor/ImGui/EditorImage.h"

#ifdef CORE_EDITOR

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Texture/Gpu/TextureGpuUploader.h"
#include "Utility/Path/ProjectPaths.h"
#include "externals/DirectXTex/DirectXTex.h"

#include <exception>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 縮小用のミップを作る
        /// @details 色にアルファを掛けた状態で縮め、掛ける前の色へ戻す
        bool BuildMipChain(const DirectX::ScratchImage& source, DirectX::ScratchImage& result)
        {
            DirectX::ScratchImage premultiplied;
            DirectX::ScratchImage mipped;
            return SUCCEEDED(DirectX::PremultiplyAlpha(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
                       DirectX::TEX_PMALPHA_DEFAULT, premultiplied))
                && SUCCEEDED(DirectX::GenerateMipMaps(premultiplied.GetImages(), premultiplied.GetImageCount(),
                       premultiplied.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipped))
                && SUCCEEDED(DirectX::PremultiplyAlpha(mipped.GetImages(), mipped.GetImageCount(), mipped.GetMetadata(),
                       DirectX::TEX_PMALPHA_REVERSE, result));
        }
    }

    EditorImage LoadEditorImage(GraphicsCore& graphics, const char* file)
    {
        EditorImage image;
        DirectX::ScratchImage source;
        if (FAILED(DirectX::LoadFromWICFile(ProjectPaths::Resolve(file).c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, source))) {
            return image;
        }
        const DirectX::TexMetadata& metadata = source.GetMetadata();
        DirectX::ScratchImage mipChain;
        const bool mipped = BuildMipChain(source, mipChain);

        try {
            const TextureGpuUploader::UploadResult uploaded =
                TextureGpuUploader::UploadAndCreateSrv(&graphics, mipped ? mipChain : source, file);
            image.texture = uploaded.texture;
            image.id = static_cast<ImTextureID>(uploaded.descriptor.gpuHandle.ptr);
            image.width = static_cast<float>(metadata.width);
            image.height = static_cast<float>(metadata.height);
        }
        catch (const std::exception&) {
            image = EditorImage{};
        }
        return image;
    }
}

#endif // CORE_EDITOR
