#include "pch.h"
#include "Editor/Export/MeshCooker.h"

#ifdef CORE_EDITOR

#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Model/Derived/ModelDerivedData.h"
#include "Graphics/Model/Import/MeshImporter.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <format>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief Windows で扱えるパスの長さの上限（終端の文字を除く）
        constexpr size_t kMaxPathLength = 259;

        std::string ToUtf8(const std::filesystem::path& path)
        {
            return Logger::GetInstance().PathToUtf8(path);
        }
    }

    std::vector<std::filesystem::path> MeshCooker::PlanTargets()
    {
        std::vector<std::filesystem::path> targets;
        for (const AssetInfo* model : AssetDatabase::GetInstance().GetAssetsOfType(AssetType::Model)) {
            if (!ModelDerivedData::ToCookedPath(model->relativePath).empty()) {
                targets.push_back(model->fullPath);
            }
        }
        return targets;
    }

    std::string MeshCooker::Cook(const std::filesystem::path& source, const std::filesystem::path& destination,
        int& writtenFiles, std::uintmax_t& writtenBytes, std::vector<CookRecord>& records)
    {
        const std::filesystem::path relative = ProjectPaths::MakeRelative(source);
        const std::filesystem::path cookedRelative = ModelDerivedData::ToCookedPath(relative);
        if (cookedRelative.empty()) {
            return {};
        }
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
        const DerivedData::Result derived = ModelDerivedData::EnsureIntermediate(source);
        if (derived.path.empty()) {
            return ToUtf8(relative) + " を .cmesh に変換できませんでした";
        }
        std::filesystem::copy_file(derived.path, cooked, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return ToUtf8(cooked) + " を書けませんでした";
        }

        const std::uintmax_t size = std::filesystem::file_size(cooked, ec);
        writtenBytes += ec ? 0 : size;
        ++writtenFiles;
        records.push_back({ ToUtf8(relative), AssetDatabase::GetInstance().GetGUID(source), ToUtf8(cookedRelative),
            MeshImporter::Settings(), derived.key });
        return {};
    }
}

#endif // CORE_EDITOR
