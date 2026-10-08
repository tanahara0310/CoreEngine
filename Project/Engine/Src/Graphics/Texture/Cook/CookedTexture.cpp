#include "pch.h"
#include "CookedTexture.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace CoreEngine
{
    namespace
    {
        constexpr std::wstring_view kSrgbSuffix = L".dds";
        constexpr std::wstring_view kLinearSuffix = L".linear.dds";

        /// @brief 小文字にする（ASCII 以外はそのまま）
        std::wstring ToLower(std::wstring text)
        {
            std::transform(text.begin(), text.end(), text.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return text;
        }
    }

    bool CookedTexture::IsCookable(const std::filesystem::path& path)
    {
        const std::wstring extension = ToLower(path.extension().wstring());
        return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
               extension == L".bmp" || extension == L".tga";
    }

    std::filesystem::path CookedTexture::ToCookedPath(const std::filesystem::path& sourceRelative,
        TextureColorSpace colorSpace)
    {
        if (!IsCookable(sourceRelative)) {
            return {};
        }
        std::filesystem::path cooked = CookedPath::FromSource(sourceRelative);
        if (cooked.empty()) {
            return {};
        }
        cooked += (colorSpace == TextureColorSpace::Linear) ? kLinearSuffix : kSrgbSuffix;
        return cooked;
    }

    std::filesystem::path CookedTexture::ToSourcePath(const std::filesystem::path& cookedRelative)
    {
        std::filesystem::path source = CookedPath::ToSource(cookedRelative);
        if (source.empty()) {
            return {};
        }

        // 末尾の `.linear.dds` か `.dds` を外す
        const std::wstring fileName = source.filename().wstring();
        const std::wstring lowerName = ToLower(fileName);
        size_t suffixLength = 0;
        if (lowerName.ends_with(kLinearSuffix)) {
            suffixLength = kLinearSuffix.size();
        } else if (lowerName.ends_with(kSrgbSuffix)) {
            suffixLength = kSrgbSuffix.size();
        } else {
            return {};
        }
        source.replace_filename(fileName.substr(0, fileName.size() - suffixLength));
        return IsCookable(source) ? source : std::filesystem::path{};
    }

    std::filesystem::path CookedTexture::Find(const std::filesystem::path& sourcePath, TextureColorSpace colorSpace)
    {
        if (!IsCookable(sourcePath)) {
            return {};
        }
        const std::filesystem::path cookedPath =
            CookedPath::Resolve(ToCookedPath(ProjectPaths::MakeRelative(sourcePath), colorSpace));
        if (cookedPath.empty()) {
            return {};
        }

        std::error_code ec;
        return std::filesystem::is_regular_file(cookedPath, ec) ? cookedPath : std::filesystem::path{};
    }
}
