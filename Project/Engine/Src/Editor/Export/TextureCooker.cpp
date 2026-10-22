#include "pch.h"
#include "Editor/Export/TextureCooker.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Model/Derived/ModelDerivedData.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Derived/TextureDerivedData.h"
#include "Graphics/Texture/Path/TexturePathResolver.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <cwctype>
#include <format>
#include <unordered_map>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief Windows で扱えるパスの長さの上限（終端の文字を除く）
        constexpr size_t kMaxPathLength = 259;

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
    }

    std::vector<CookTarget> TextureCooker::PlanTargets(const std::vector<std::filesystem::path>& sources)
    {
        // モデルの .cmesh のマテリアルと .mat から、画像ごとの使い道を集める（テクスチャのパスの解き方は実行時と同じ）
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
            MeshAsset asset;
            if (!ModelDerivedData::Load(model->fullPath, asset)) {
                continue;
            }
            for (const MaterialAsset& material : asset.model.materials) {
                mark(material.baseColorTexture, false);
                mark(material.emissiveTexture, false);
                mark(material.metallicRoughnessTexture, true);
                mark(material.normalTexture, true);
                mark(material.occlusionTexture, true);
            }
        }
        std::vector<MaterialTexture> materialTextures;
        for (const AssetInfo* material : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Material)) {
            if (material->fullPath.extension() != ".mat" || !ShaderMaterial::ReadTextures(material->fullPath, materialTextures)) {
                continue;
            }
            for (const MaterialTexture& texture : materialTextures) {
                mark(texture.path, texture.linear);
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

            // 取り込み設定で色空間を決めた画像は、その版だけを作る
            const std::string setting = AssetDatabase::GetInstance().GetImportColorSpace(ToUtf8(source));
            if (!setting.empty()) {
                const bool linear = ApplyImportColorSpace(setting, TextureColorSpace::SRGB) == TextureColorSpace::Linear;
                target.linear = linear;
                target.srgb = !linear;
            }
            targets.push_back(std::move(target));
        }
        return targets;
    }

    std::string TextureCooker::Cook(const CookTarget& target, const std::filesystem::path& destination,
        int& writtenFiles, std::uintmax_t& writtenBytes, std::vector<CookRecord>& records)
    {
        const std::string guid = AssetDatabase::GetInstance().GetGUID(target.source);

        for (const TextureColorSpace colorSpace : { TextureColorSpace::SRGB, TextureColorSpace::Linear }) {
            if ((colorSpace == TextureColorSpace::SRGB && !target.srgb) ||
                (colorSpace == TextureColorSpace::Linear && !target.linear)) {
                continue;
            }

            const std::filesystem::path cookedRelative = CookedTexture::ToCookedPath(target.relative, colorSpace);
            const std::filesystem::path cooked = destination / cookedRelative;
            if (cooked.native().size() > kMaxPathLength) {
                return std::format("{} は {} 文字あり、Windows のパスの上限（{} 文字）を超えるので書けません。"
                    "プロジェクトをもっと短いパスの場所へ移してください",
                    ToUtf8(cooked), cooked.native().size(), kMaxPathLength);
            }
            std::error_code ec;
            std::filesystem::create_directories(cooked.parent_path(), ec);
            if (ec) {
                return ToUtf8(cooked.parent_path()) + " を作れませんでした";
            }

            // 開発中の派生データを新しくしてから写す
            const DerivedData::Result derived = TextureDerivedData::EnsureIntermediate(target.source, colorSpace);
            if (derived.path.empty()) {
                return ToUtf8(target.relative) + " を DDS に変換できませんでした";
            }
            std::filesystem::copy_file(derived.path, cooked, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return ToUtf8(cooked) + " を書けませんでした";
            }

            const std::uintmax_t size = std::filesystem::file_size(cooked, ec);
            writtenBytes += ec ? 0 : size;
            ++writtenFiles;
            records.push_back({ ToUtf8(target.relative), guid, ToUtf8(cookedRelative),
                TextureDerivedData::Settings(target.source, colorSpace), derived.key });
        }
        return {};
    }
}

#endif // CORE_EDITOR
