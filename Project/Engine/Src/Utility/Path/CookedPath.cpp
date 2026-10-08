#include "pch.h"
#include "CookedPath.h"
#include "ProjectPaths.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace CoreEngine
{
    namespace
    {
        /// @brief 綴りの 1 区切りが name と一致するか（大文字と小文字は区別しない）
        bool IsNamed(const std::filesystem::path& part, std::wstring_view name)
        {
            const std::wstring text = part.wstring();
            return std::equal(text.begin(), text.end(), name.begin(), name.end(),
                [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
        }

        /// @brief 綴りの 2 番目の区切りを置き換える（`<根>/<from>/…` → `<根>/<to>/…`）
        /// @return 先頭が `Application` か `Engine`、2 番目が from でない、またはその下が無ければ空
        std::filesystem::path ReplaceSecondPart(const std::filesystem::path& relative,
            std::wstring_view from, std::wstring_view to)
        {
            auto it = relative.begin();
            if (it == relative.end() || !(IsNamed(*it, L"Application") || IsNamed(*it, L"Engine"))) {
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

    std::filesystem::path CookedPath::FromSource(const std::filesystem::path& sourceRelative)
    {
        return ReplaceSecondPart(sourceRelative, L"Assets", L"Cooked");
    }

    std::filesystem::path CookedPath::ToSource(const std::filesystem::path& cookedRelative)
    {
        return ReplaceSecondPart(cookedRelative, L"Cooked", L"Assets");
    }

    std::filesystem::path CookedPath::Resolve(const std::filesystem::path& relative)
    {
        if (relative.empty()) {
            return {};
        }
        const std::filesystem::path top = *relative.begin();
        if (IsNamed(top, L"Application")) {
            return ProjectPaths::ProjectRoot() / relative;
        }
        if (IsNamed(top, L"Engine")) {
            return ProjectPaths::EngineRoot() / relative;
        }
        return {};
    }
}
