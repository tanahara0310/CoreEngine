#include "pch.h"
#include "TextureDerivedData.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Import/TextureImporter.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kKind = "Texture";

        TextureColorSpace Other(TextureColorSpace colorSpace)
        {
            return (colorSpace == TextureColorSpace::Linear) ? TextureColorSpace::SRGB : TextureColorSpace::Linear;
        }

        /// @brief 画像の圧縮の形式（取り込み設定が無ければ既定）
        TextureCompression CompressionOf(const std::filesystem::path& source)
        {
            return TextureImporter::ParseCompression(
                AssetDatabase::GetInstance().GetImportFormat(Logger::GetInstance().PathToUtf8(source)));
        }

        DerivedData::Request MakeRequest(const std::filesystem::path& source, TextureColorSpace colorSpace,
            TextureCompression compression)
        {
            DerivedData::Request request;
            request.kind = kKind;
            request.source = source;
            request.intermediate = TextureDerivedData::IntermediatePath(source, colorSpace);
            request.settings = TextureImporter::Settings(colorSpace, compression);
            request.importerVersion = TextureImporter::kVersion;
            return request;
        }

        DerivedData::Importer MakeImporter(TextureColorSpace colorSpace, TextureCompression compression)
        {
            return [colorSpace, compression](const std::filesystem::path& source, const std::filesystem::path& output) {
                return TextureImporter::Import(source, output, colorSpace, compression);
            };
        }
    }

    std::filesystem::path TextureDerivedData::IntermediatePath(const std::filesystem::path& source, TextureColorSpace colorSpace)
    {
        return DerivedData::IntermediatePath(kKind, source,
            (colorSpace == TextureColorSpace::Linear) ? ".linear.dds" : ".srgb.dds");
    }

    DerivedData::Result TextureDerivedData::Find(const std::filesystem::path& source, TextureColorSpace colorSpace)
    {
        const TextureCompression compression = CompressionOf(source);
        DerivedData::Request request = MakeRequest(source, colorSpace, compression);
        const std::filesystem::path relative = ProjectPaths::MakeRelative(source);
        if (!relative.empty()) {
            request.cooked = CookedPath::Resolve(CookedTexture::ToCookedPath(relative, colorSpace));
            request.cookedFallback = CookedPath::Resolve(CookedTexture::ToCookedPath(relative, Other(colorSpace)));
        }
        return DerivedData::Find(request, MakeImporter(colorSpace, compression));
    }

    DerivedData::Result TextureDerivedData::EnsureIntermediate(const std::filesystem::path& source, TextureColorSpace colorSpace)
    {
        const TextureCompression compression = CompressionOf(source);
        return DerivedData::EnsureIntermediate(MakeRequest(source, colorSpace, compression), MakeImporter(colorSpace, compression));
    }

    std::string TextureDerivedData::Settings(const std::filesystem::path& source, TextureColorSpace colorSpace)
    {
        return TextureImporter::Settings(colorSpace, CompressionOf(source));
    }
}
