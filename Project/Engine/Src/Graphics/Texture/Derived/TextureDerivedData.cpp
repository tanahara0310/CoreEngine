#include "pch.h"
#include "TextureDerivedData.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Import/TextureImporter.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

#include <vector>

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

    void TextureDerivedData::RemoveLegacyCache()
    {
        const std::filesystem::path folder = ProjectPaths::Intermediate("TextureCache");
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) {
            return;
        }

        // 先に消す物を集めてから消す（キューブマップは残す）
        std::vector<std::filesystem::path> targets;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            const std::filesystem::path& file = entry.path();
            if (entry.is_regular_file(ec) && file.extension() == ".dds"
                && file.filename().wstring().find(L"_cubemap") == std::wstring::npos) {
                targets.push_back(file);
            }
        }

        size_t removed = 0;
        std::uintmax_t bytes = 0;
        for (const std::filesystem::path& file : targets) {
            std::error_code sizeError;
            const std::uintmax_t size = std::filesystem::file_size(file, sizeError);
            if (std::filesystem::remove(file, ec)) {
                ++removed;
                bytes += sizeError ? 0 : size;
            }
        }
        if (removed > 0) {
            Logger& log = Logger::GetInstance();
            log.Logf(LogLevel::INFO, LogCategory::Resource, "古いテクスチャのキャッシュを {} 個（{:.1f} MB）消しました: {}",
                removed, static_cast<double>(bytes) / (1024.0 * 1024.0), log.PathToUtf8(folder));
        }
    }
}
