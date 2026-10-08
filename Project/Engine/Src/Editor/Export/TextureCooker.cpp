#include "pch.h"
#include "Editor/Export/TextureCooker.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Model/ModelLoader.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Generate/TextureDdsCacheGenerator.h"
#include "Graphics/Texture/Path/TexturePathResolver.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <cwctype>
#include <unordered_map>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief 画像の使い道
        struct Usage
        {
            bool srgb = false;
            bool linear = false;
        };

        /// @brief パスを照合用のキーにする
        std::wstring MakeKey(const std::filesystem::path& path)
        {
            std::wstring key = path.lexically_normal().wstring();
            for (wchar_t& c : key) {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            return key;
        }

        /// @brief path を UTF-8 にする
        std::string ToUtf8(const std::filesystem::path& path)
        {
            return Logger::GetInstance().PathToUtf8(path);
        }

        /// @brief DDS が元の画像より新しいか
        bool IsUpToDate(const std::filesystem::path& dds, const std::filesystem::path& source)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(dds, ec)) {
                return false;
            }
            const auto ddsTime = std::filesystem::last_write_time(dds, ec);
            if (ec) {
                return false;
            }
            const auto sourceTime = std::filesystem::last_write_time(source, ec);
            return !ec && sourceTime <= ddsTime;
        }
    }

    std::vector<CookTarget> TextureCooker::PlanTargets(const std::vector<std::filesystem::path>& sources)
    {
        // モデルのマテリアルから、画像ごとの使い道を集める（テクスチャのパスの解き方は実行時と同じ）
        std::unordered_map<std::wstring, Usage> usages;
        const TexturePathResolver resolver;
        const auto mark = [&](const std::string& texture, bool linear) {
            if (texture.empty()) {
                return;
            }
            Usage& usage = usages[MakeKey(resolver.ResolveAssetPath(texture))];
            (linear ? usage.linear : usage.srgb) = true;
        };
        for (const AssetInfo* model : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Model)) {
            const std::vector<MaterialAsset> materials = ModelLoader::LoadMaterialsOnly(
                ToUtf8(model->fullPath.parent_path()), ToUtf8(model->fullPath.filename()));
            for (const MaterialAsset& material : materials) {
                mark(material.baseColorTexture, false);
                mark(material.emissiveTexture, false);
                mark(material.metallicRoughnessTexture, true);
                mark(material.normalTexture, true);
                mark(material.occlusionTexture, true);
            }
        }

        std::vector<CookTarget> targets;
        targets.reserve(sources.size());
        for (const std::filesystem::path& source : sources) {
            CookTarget target;
            target.source = source;
            target.relative = ProjectPaths::MakeRelative(source);
            if (target.relative.empty() || !CookedTexture::IsCookable(source)) {
                continue;
            }
            const auto found = usages.find(MakeKey(source));
            const Usage usage = (found != usages.end()) ? found->second : Usage{};
            target.linear = usage.linear;
            target.srgb = usage.srgb || !usage.linear;
            targets.push_back(std::move(target));
        }
        return targets;
    }

    std::string TextureCooker::Cook(const CookTarget& target, const std::filesystem::path& destination,
        int& writtenFiles, std::uintmax_t& writtenBytes)
    {
        const TexturePathResolver resolver;
        const TextureDdsCacheGenerator generator;
        const bool hasGuid = !AssetDatabase::GetInstance().GetGUID(target.source).empty();

        for (const TextureColorSpace colorSpace : { TextureColorSpace::SRGB, TextureColorSpace::Linear }) {
            if ((colorSpace == TextureColorSpace::SRGB && !target.srgb) ||
                (colorSpace == TextureColorSpace::Linear && !target.linear)) {
                continue;
            }

            const std::filesystem::path cooked =
                destination / CookedTexture::ToCookedPath(target.relative, colorSpace);
            std::error_code ec;
            std::filesystem::create_directories(cooked.parent_path(), ec);
            if (ec) {
                return ToUtf8(cooked.parent_path()) + " を作れませんでした";
            }

            if (hasGuid) {
                // 開発中の DDS キャッシュを作り直してから写す
                const std::filesystem::path cache = resolver.GetDDSCachePath(target.source, colorSpace);
                if (!IsUpToDate(cache, target.source) && !generator.GenerateCache(target.source, cache, colorSpace)) {
                    return ToUtf8(target.relative) + " を DDS に変換できませんでした";
                }
                std::filesystem::copy_file(cache, cooked, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) {
                    return ToUtf8(cooked) + " を書けませんでした";
                }
            } else if (!generator.GenerateCache(target.source, cooked, colorSpace)) {
                return ToUtf8(target.relative) + " を DDS に変換できませんでした";
            }

            const std::uintmax_t size = std::filesystem::file_size(cooked, ec);
            writtenBytes += ec ? 0 : size;
            ++writtenFiles;
        }
        return {};
    }
}

#endif // CORE_EDITOR
