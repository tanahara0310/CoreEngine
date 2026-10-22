#include "pch.h"
#include "TextureDerivedData.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Import/TextureImporter.h"
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

        DerivedData::Request MakeRequest(const std::filesystem::path& source, TextureColorSpace colorSpace)
        {
            DerivedData::Request request;
            request.kind = kKind;
            request.source = source;
            request.intermediate = TextureDerivedData::IntermediatePath(source, colorSpace);
            request.settings = TextureImporter::Settings(colorSpace);
            request.importerVersion = TextureImporter::kVersion;
            return request;
        }

        DerivedData::Importer MakeImporter(TextureColorSpace colorSpace)
        {
            return [colorSpace](const std::filesystem::path& source, const std::filesystem::path& output) {
                return TextureImporter::Import(source, output, colorSpace);
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
        DerivedData::Request request = MakeRequest(source, colorSpace);
        const std::filesystem::path relative = ProjectPaths::MakeRelative(source);
        if (!relative.empty()) {
            request.cooked = CookedPath::Resolve(CookedTexture::ToCookedPath(relative, colorSpace));
            request.cookedFallback = CookedPath::Resolve(CookedTexture::ToCookedPath(relative, Other(colorSpace)));
        }
        return DerivedData::Find(request, MakeImporter(colorSpace));
    }

    DerivedData::Result TextureDerivedData::EnsureIntermediate(const std::filesystem::path& source, TextureColorSpace colorSpace)
    {
        return DerivedData::EnsureIntermediate(MakeRequest(source, colorSpace), MakeImporter(colorSpace));
    }
}
