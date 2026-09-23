#include "pch.h"
#include "Editor/External/ExternalCodeEditor.h"

#include "Utility/Logger/Logger.h"

#include <Windows.h>
#include <shellapi.h>

#include <format>
#include <string>

namespace CoreEngine::Editor
{
    namespace
    {
        /// @brief URL に入れられない文字を %XX にする（区切りの / と : は残す）
        std::string EncodeForUrl(const std::string& utf8)
        {
            std::string encoded;
            encoded.reserve(utf8.size());
            for (const char c : utf8) {
                const unsigned char byte = static_cast<unsigned char>(c);
                const bool keep = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                    || (byte >= '0' && byte <= '9')
                    || byte == '-' || byte == '_' || byte == '.' || byte == '~'
                    || byte == '/' || byte == ':';
                if (keep) {
                    encoded.push_back(c);
                } else {
                    encoded += std::format("%{:02X}", byte);
                }
            }
            return encoded;
        }
    }

    bool OpenInCodeEditor(const std::filesystem::path& file, int line, int column)
    {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(file, error);
        if (error || !std::filesystem::exists(absolute, error)) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "開くファイルが見つかりません: {}", Logger::GetInstance().PathToUtf8(file));
            return false;
        }

        // VS Code の URL（vscode://file/<パス>:<行>:<桁>）で開く
        std::string url = "vscode://file/" + EncodeForUrl(Logger::GetInstance().PathToUtf8(absolute));
        if (line > 0) {
            url += std::format(":{}", line);
            if (column > 0) {
                url += std::format(":{}", column);
            }
        }

        // 符号化した後の URL は ASCII だけなので、1 文字ずつ広げてよい
        const std::wstring wideUrl(url.begin(), url.end());
        const INT_PTR result = reinterpret_cast<INT_PTR>(
            ::ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "VS Code を開けませんでした（{}）: {}", result, url);
            return false;
        }
        return true;
    }
}
