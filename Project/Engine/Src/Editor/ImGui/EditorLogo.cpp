#include "pch.h"
#include "Editor/ImGui/EditorLogo.h"

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
        /// Kind の並びと同じ順の画像
        constexpr const char* kFiles[] = {
            "Engine/Assets/Textures/Logo/CoreEngineMark.png",
            "Engine/Assets/Textures/Logo/CoreEngineLogo.png",
            "Engine/Assets/Textures/Logo/CoreEngineLogoStacked.png",
        };

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

    EditorLogo::EditorLogo(GraphicsCore& graphics)
        : graphics_(graphics)
    {
    }

    ImTextureID EditorLogo::Texture(Kind kind)
    {
        return Get(kind).id;
    }

    float EditorLogo::Aspect(Kind kind)
    {
        return Get(kind).aspect;
    }

    const EditorLogo::Image& EditorLogo::Get(Kind kind)
    {
        Image& image = images_[static_cast<size_t>(kind)];
        if (image.attempted) {
            return image;
        }
        // 読めなかったときも覚えて、次からは読み直さない
        image.attempted = true;

        const char* const file = kFiles[static_cast<size_t>(kind)];
        DirectX::ScratchImage source;
        if (FAILED(DirectX::LoadFromWICFile(ProjectPaths::Resolve(file).c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, source))) {
            return image;
        }
        const DirectX::TexMetadata& metadata = source.GetMetadata();
        DirectX::ScratchImage mipChain;
        const bool mipped = BuildMipChain(source, mipChain);

        try {
            const TextureGpuUploader::UploadResult uploaded =
                TextureGpuUploader::UploadAndCreateSrv(&graphics_, mipped ? mipChain : source, file);
            image.texture = uploaded.texture;
            image.id = static_cast<ImTextureID>(uploaded.descriptor.gpuHandle.ptr);
            image.aspect = static_cast<float>(metadata.width) / static_cast<float>(metadata.height);
        }
        catch (const std::exception&) {
            image.texture.Reset();
            image.id = 0;
        }
        return image;
    }
}

#endif // CORE_EDITOR
