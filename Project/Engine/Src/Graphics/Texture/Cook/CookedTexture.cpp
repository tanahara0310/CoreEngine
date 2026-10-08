#include "pch.h"
#include "CookedTexture.h"
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

        /// @brief 綴りの 1 区切りが name と一致するか（大文字と小文字は区別しない）
        bool IsNamed(const std::filesystem::path& part, std::wstring_view name)
        {
            return ToLower(part.wstring()) == ToLower(std::wstring(name));
        }

        /// @brief 先頭の区切りが `Application` か `Engine` か
        bool IsRootName(const std::filesystem::path& part)
        {
            return IsNamed(part, L"Application") || IsNamed(part, L"Engine");
        }

        /// @brief 綴りの先頭 2 区切りを置き換える（`<根>/<from>/…` → `<根>/<to>/…`）
        /// @return 先頭が `<根>/<from>` でない、またはその下が無ければ空
        std::filesystem::path ReplaceSecondPart(const std::filesystem::path& relative,
            std::wstring_view from, std::wstring_view to)
        {
            auto it = relative.begin();
            if (it == relative.end() || !IsRootName(*it)) {
                return {};
            }
            std::filesystem::path result = *it;
            ++it;
            if (it == relative.end() || !IsNamed(*it, from)) {
                return {};
            }
            result /= to;
            ++it;
            if (it == relative.end()) {
                return {};
            }
            for (; it != relative.end(); ++it) {
                result /= *it;
            }
            return result;
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
        std::filesystem::path cooked = ReplaceSecondPart(sourceRelative, L"Assets", L"Cooked");
        if (cooked.empty()) {
            return {};
        }
        cooked += (colorSpace == TextureColorSpace::Linear) ? kLinearSuffix : kSrgbSuffix;
        return cooked;
    }

    std::filesystem::path CookedTexture::ToSourcePath(const std::filesystem::path& cookedRelative)
    {
        std::filesystem::path source = ReplaceSecondPart(cookedRelative, L"Cooked", L"Assets");
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
        const std::filesystem::path sourceRelative = ProjectPaths::MakeRelative(sourcePath);
        const std::filesystem::path cookedRelative = ToCookedPath(sourceRelative, colorSpace);
        if (cookedRelative.empty()) {
            return {};
        }

        // `Application/…` はプロジェクトの根、`Engine/…` はエンジンの根の下にある
        const std::filesystem::path& root = IsNamed(*cookedRelative.begin(), L"Application")
            ? ProjectPaths::ProjectRoot()
            : ProjectPaths::EngineRoot();
        const std::filesystem::path cookedPath = root / cookedRelative;

        std::error_code ec;
        return std::filesystem::is_regular_file(cookedPath, ec) ? cookedPath : std::filesystem::path{};
    }
}
