#include "pch.h"
#include "Editor/Export/ThirdPartyNotices.h"

#ifdef CORE_EDITOR

#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr std::string_view kSeparator =
            "================================================================================\r\n";

        /// @brief 改行を CRLF にそろえ、末尾の空行を 1 つの改行にする
        std::string ToCrlf(std::string_view text)
        {
            std::string out;
            out.reserve(text.size() + text.size() / 32);
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (c == '\r') {
                    continue;
                }
                if (c == '\n') {
                    out += "\r\n";
                    continue;
                }
                out += c;
            }
            while (out.ends_with("\r\n")) {
                out.resize(out.size() - 2);
            }
            out += "\r\n";
            return out;
        }

        /// @brief ファイルを丸ごと読む（UTF-8 の BOM は除く）
        bool ReadText(const std::filesystem::path& path, std::string& text)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return false;
            }
            text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            if (text.starts_with("\xEF\xBB\xBF")) {
                text.erase(0, 3);
            }
            return true;
        }

        /// @brief 一覧の文字列の項目を読む（無ければ空）
        std::string StringField(const json& entry, const char* key)
        {
            const auto it = entry.find(key);
            return (it != entry.end() && it->is_string()) ? it->get<std::string>() : std::string();
        }
    }

    std::string ThirdPartyNotices::Build(std::string& error)
    {
        error.clear();
        Logger& log = Logger::GetInstance();
        const std::filesystem::path externals = ProjectPaths::EngineRoot() / "Externals";
        const std::filesystem::path listPath = externals / "ThirdParty.json";
        const json list = JsonManager::ParseTextFile(listPath);
        const auto libraries = list.is_object() ? list.find("libraries") : list.end();
        if (list.is_discarded() || !list.is_object() || libraries == list.end() || !libraries->is_array()) {
            error = "外部ライブラリの一覧 " + log.PathToUtf8(listPath) + " を読めませんでした";
            return {};
        }

        std::string text =
            "THIRD-PARTY SOFTWARE NOTICES\r\n"
            "\r\n"
            "This software uses the following third-party software.\r\n"
            "このソフトウェアは次の外部のソフトウェアを使っています。\r\n"
            "\r\n";
        for (const json& library : *libraries) {
            if (!library.is_object() || !library.value("inGame", false)) {
                continue;
            }
            const std::string name = StringField(library, "name");
            const std::string version = StringField(library, "version");
            text += kSeparator;
            text += version.empty() ? name : name + " " + version;
            text += "\r\n";
            if (const std::string source = StringField(library, "source"); !source.empty()) {
                text += source + "\r\n";
            }
            if (const std::string license = StringField(library, "license"); !license.empty()) {
                text += "License: " + license + "\r\n";
            }
            text += kSeparator;

            const auto files = library.find("licenseFiles");
            if (files == library.end() || !files->is_array()) {
                continue;
            }
            for (const json& file : *files) {
                if (!file.is_string()) {
                    error = name + " の licenseFiles に文字列でない項目があります";
                    return {};
                }
                const std::filesystem::path path = externals / log.Utf8ToPath(file.get<std::string>());
                std::string body;
                if (!ReadText(path, body)) {
                    error = name + " のライセンスのファイル " + log.PathToUtf8(path) + " を読めませんでした";
                    return {};
                }
                text += "\r\n";
                text += ToCrlf(body);
            }
            text += "\r\n";
        }
        return text;
    }
}

#endif // CORE_EDITOR
