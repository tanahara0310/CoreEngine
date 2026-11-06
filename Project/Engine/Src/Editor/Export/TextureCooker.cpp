#include "pch.h"
#include "Editor/Export/TextureCooker.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Model/Derived/ModelDerivedData.h"
#include "Graphics/Texture/Cook/CookedTexture.h"
#include "Graphics/Texture/Derived/TextureDerivedData.h"
#include "Graphics/Texture/Path/TexturePathResolver.h"
#include "Utility/Compression/CompressedFile.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <cwctype>
#include <format>
#include <fstream>
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
            bool normal = false;
        };

        /// @brief 色空間の版を作る印を立てる
        void Mark(CookTarget& target, TextureColorSpace colorSpace)
        {
            switch (colorSpace) {
            case TextureColorSpace::Linear: target.linear = true; break;
            case TextureColorSpace::Normal: target.normal = true; break;
            default: target.srgb = true; break;
            }
        }

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
        const auto mark = [&](const std::string& texture, TextureColorSpace colorSpace) {
            if (texture.empty()) {
                return;
            }
            Usage& usage = usages[MakeKey(resolver.ResolveAssetPath(texture))];
            switch (colorSpace) {
            case TextureColorSpace::Linear: usage.linear = true; break;
            case TextureColorSpace::Normal: usage.normal = true; break;
            default: usage.srgb = true; break;
            }
        };
        for (const AssetInfo* model : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Model)) {
            MeshAsset asset;
            if (!ModelDerivedData::Load(model->fullPath, asset)) {
                continue;
            }
            for (const MaterialAsset& material : asset.model.materials) {
                mark(material.baseColorTexture, TextureColorSpace::SRGB);
                mark(material.emissiveTexture, TextureColorSpace::SRGB);
                mark(material.metallicRoughnessTexture, TextureColorSpace::Linear);
                mark(material.normalTexture, TextureColorSpace::Normal);
                mark(material.occlusionTexture, TextureColorSpace::Linear);
            }
        }
        std::vector<MaterialTexture> materialTextures;
        for (const AssetInfo* material : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Material)) {
            if (material->fullPath.extension() != ".mat" || !ShaderMaterial::ReadTextures(material->fullPath, materialTextures)) {
                continue;
            }
            for (const MaterialTexture& texture : materialTextures) {
                mark(texture.path, texture.linear ? TextureColorSpace::Linear : TextureColorSpace::SRGB);
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

            // 使い道ごとに、取り込み設定を当てた後の色空間（実行時に読む版）を作る
            const std::string setting = AssetDatabase::GetInstance().GetImportColorSpace(ToUtf8(source));
            if (usage.srgb || (!usage.linear && !usage.normal)) {
                Mark(target, ApplyImportColorSpace(setting, TextureColorSpace::SRGB));
            }
            if (usage.linear) {
                Mark(target, ApplyImportColorSpace(setting, TextureColorSpace::Linear));
            }
            if (usage.normal) {
                Mark(target, ApplyImportColorSpace(setting, TextureColorSpace::Normal));
            }
            targets.push_back(std::move(target));
        }
        return targets;
    }

    std::string TextureCooker::Cook(const CookTarget& target, const std::filesystem::path& destination,
        int& writtenFiles, std::uintmax_t& writtenBytes, std::vector<CookRecord>& records)
    {
        const std::string guid = AssetDatabase::GetInstance().GetGUID(target.source);

        for (const TextureColorSpace colorSpace : { TextureColorSpace::SRGB, TextureColorSpace::Linear, TextureColorSpace::Normal }) {
            if ((colorSpace == TextureColorSpace::SRGB && !target.srgb) ||
                (colorSpace == TextureColorSpace::Linear && !target.linear) ||
                (colorSpace == TextureColorSpace::Normal && !target.normal)) {
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

            // 開発中の派生データを新しくしてから、圧縮して写す
            const DerivedData::Result derived = TextureDerivedData::EnsureIntermediate(target.source, colorSpace);
            if (derived.path.empty()) {
                return ToUtf8(target.relative) + " を DDS に変換できませんでした";
            }
            if (!CompressedFile::WriteCompressed(derived.path, cooked)) {
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

    std::string TextureCooker::MergeDuplicates(const std::filesystem::path& destination, std::vector<CookRecord>& records,
        int& removedFiles, std::uintmax_t& removedBytes)
    {
        // 派生キーごとに、綴りがいちばん前の DDS を残す（どのスレッドが先に終わっても同じ結果にする）
        std::unordered_map<std::string, std::string> kept;
        for (const CookRecord& record : records) {
            if (record.key.empty()) {
                continue;
            }
            auto [it, inserted] = kept.try_emplace(record.key, record.derived);
            if (!inserted && record.derived < it->second) {
                it->second = record.derived;
            }
        }

        Logger& log = Logger::GetInstance();
        std::string aliases;
        for (CookRecord& record : records) {
            const auto found = record.key.empty() ? kept.end() : kept.find(record.key);
            if (found == kept.end() || found->second == record.derived) {
                continue;
            }
            const std::filesystem::path duplicate = destination / log.Utf8ToPath(record.derived);
            std::error_code ec;
            const std::uintmax_t size = std::filesystem::file_size(duplicate, ec);
            if (!std::filesystem::remove(duplicate, ec) || ec) {
                return ToUtf8(duplicate) + " を消せませんでした";
            }
            ++removedFiles;
            removedBytes += size;
            aliases += std::format("{}\t{}\n", record.derived, found->second);
            record.derived = found->second;
        }
        if (aliases.empty()) {
            return {};
        }

        const std::filesystem::path aliasFile = destination / CookedTexture::kAliasFileName;
        std::ofstream file(aliasFile, std::ios::binary | std::ios::trunc);
        file.write(aliases.data(), static_cast<std::streamsize>(aliases.size()));
        return file ? std::string() : ToUtf8(aliasFile) + " を書けませんでした";
    }
}

#endif // CORE_EDITOR
