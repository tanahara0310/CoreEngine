#include "pch.h"
#include "CookedShader.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

#include <string>

namespace CoreEngine
{
    std::filesystem::path CookedShader::ToCookedPath(const std::filesystem::path& sourceRelative,
        std::wstring_view profile, std::wstring_view entryPoint)
    {
        if (profile.empty()) {
            return {};
        }
        std::filesystem::path cooked = CookedPath::FromSource(sourceRelative);
        if (cooked.empty()) {
            return {};
        }

        std::wstring suffix = L".";
        suffix += profile;
        if (!entryPoint.empty() && entryPoint != L"main") {
            suffix += L".";
            suffix += entryPoint;
        }
        suffix += L".dxil";
        cooked += suffix;
        return cooked;
    }

    std::filesystem::path CookedShader::Find(const std::filesystem::path& sourcePath,
        std::wstring_view profile, std::wstring_view entryPoint)
    {
        const std::filesystem::path cookedPath =
            CookedPath::Resolve(ToCookedPath(ProjectPaths::MakeRelative(sourcePath), profile, entryPoint));
        if (cookedPath.empty()) {
            return {};
        }

        std::error_code ec;
        return std::filesystem::is_regular_file(cookedPath, ec) ? cookedPath : std::filesystem::path{};
    }
}
