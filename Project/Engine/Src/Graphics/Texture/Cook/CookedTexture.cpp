#include "pch.h"
#include "CookedTexture.h"
#include "Utility/Path/CookedPath.h"
#include "Utility/Path/ProjectPaths.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

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

        /// @brief 綴りを照合用のキーにする（正規化して小文字）
        std::wstring MakeKey(const std::filesystem::path& path)
        {
            return ToLower(path.lexically_normal().generic_wstring());
        }

        /// @brief UTF-8 の文字列をパスにする
        std::filesystem::path FromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
        }

        /// @brief DDS の別名の一覧
        struct AliasTable
        {
            std::unordered_map<std::wstring, std::filesystem::path> targets; ///< 別名のキー → 実際に置いた DDS の綴り
            std::vector<std::filesystem::path> aliases;                      ///< 別名の綴り（一覧の順）
        };

        /// @brief 書き出したゲームの根の別名の一覧を 1 度だけ読む（書き出したゲームでなければ空）
        const AliasTable& Aliases()
        {
            static const AliasTable table = [] {
                AliasTable result;
                if (!CookedPath::IsCookedBuild()) {
                    return result;
                }
                std::ifstream file(ProjectPaths::EngineRoot() / CookedTexture::kAliasFileName, std::ios::binary);
                std::string line;
                while (std::getline(file, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    const size_t tab = line.find('\t');
                    if (tab == std::string::npos) {
                        continue;
                    }
                    const std::filesystem::path alias = FromUtf8(std::string_view(line).substr(0, tab));
                    result.targets.emplace(MakeKey(alias), FromUtf8(std::string_view(line).substr(tab + 1)));
                    result.aliases.push_back(alias);
                }
                return result;
            }();
            return table;
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

    std::filesystem::path CookedTexture::ResolveCookedPath(const std::filesystem::path& cookedRelative)
    {
        if (cookedRelative.empty()) {
            return {};
        }
        const AliasTable& table = Aliases();
        if (!table.targets.empty()) {
            if (const auto found = table.targets.find(MakeKey(cookedRelative)); found != table.targets.end()) {
                return CookedPath::Resolve(found->second);
            }
        }
        return CookedPath::Resolve(cookedRelative);
    }

    std::vector<std::filesystem::path> CookedTexture::AliasPaths()
    {
        return Aliases().aliases;
    }

    std::filesystem::path CookedTexture::Find(const std::filesystem::path& sourcePath, TextureColorSpace colorSpace)
    {
        if (!IsCookable(sourcePath)) {
            return {};
        }
        const std::filesystem::path cookedPath =
            ResolveCookedPath(ToCookedPath(ProjectPaths::MakeRelative(sourcePath), colorSpace));
        if (cookedPath.empty()) {
            return {};
        }

        std::error_code ec;
        return std::filesystem::is_regular_file(cookedPath, ec) ? cookedPath : std::filesystem::path{};
    }
}
