#include "pch.h"
#include "ModelDerivedData.h"
#include "Graphics/Model/Import/CmeshFile.h"
#include "Graphics/Model/Import/MeshImporter.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kKind = "Mesh";
        constexpr const char* kExtension = ".cmesh";

        DerivedData::Request MakeRequest(const std::filesystem::path& source)
        {
            DerivedData::Request request;
            request.kind = kKind;
            request.source = source;
            request.intermediate = DerivedData::IntermediatePath(kKind, source, kExtension);
            request.settings = MeshImporter::Settings();
            request.importerVersion = MeshImporter::kVersion;
            return request;
        }

        bool Import(const std::filesystem::path& source, const std::filesystem::path& output)
        {
            return MeshImporter::Import(source, output);
        }
    }

    std::filesystem::path ModelDerivedData::ToCookedPath(const std::filesystem::path& sourceRelative)
    {
        std::filesystem::path cooked = CookedPath::FromSource(sourceRelative);
        if (!cooked.empty()) {
            cooked += kExtension;
        }
        return cooked;
    }

    DerivedData::Result ModelDerivedData::Find(const std::filesystem::path& source)
    {
        DerivedData::Request request = MakeRequest(source);
        if (const std::filesystem::path relative = ProjectPaths::MakeRelative(source); !relative.empty()) {
            request.cooked = CookedPath::Resolve(ToCookedPath(relative));
        }
        return DerivedData::Find(request, Import);
    }

    DerivedData::Result ModelDerivedData::EnsureIntermediate(const std::filesystem::path& source)
    {
        return DerivedData::EnsureIntermediate(MakeRequest(source), Import);
    }

    bool ModelDerivedData::Load(const std::filesystem::path& source, MeshAsset& asset)
    {
        DerivedData::Result derived = Find(source);
        if (derived.path.empty()) {
            return false;
        }
        if (CmeshFile::Read(derived.path, asset)) {
            return true;
        }

        // 開発中の置き場の物が読めなければ、消して作り直す
        Logger& log = Logger::GetInstance();
        if (derived.origin != DerivedData::Origin::Cooked && !CookedPath::IsCookedBuild()) {
            std::error_code ec;
            std::filesystem::remove(derived.path, ec);
            derived = EnsureIntermediate(source);
            if (!derived.path.empty() && CmeshFile::Read(derived.path, asset)) {
                return true;
            }
        }
        log.Logf(LogLevel::Error, LogCategory::Resource, "モデルの .cmesh を読めません: {}", log.PathToUtf8(derived.path));
        return false;
    }
}
