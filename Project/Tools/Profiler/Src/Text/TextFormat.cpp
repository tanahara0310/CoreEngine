#include "Text/TextFormat.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace CoreEngine::Profiler::Text
{
    namespace
    {
        /// @brief 整数の部分に 3 桁ごとのカンマを入れる（"26743.4" → "26,743.4"）
        std::string InsertThousandsSeparators(std::string digits)
        {
            const size_t dot = digits.find('.');
            const size_t integerEnd = (dot == std::string::npos) ? digits.size() : dot;
            const size_t integerBegin = (!digits.empty() && digits[0] == '-') ? 1 : 0;
            for (size_t i = integerEnd; i > integerBegin + 3; i -= 3) {
                digits.insert(i - 3, 1, ',');
            }
            return digits;
        }

        char ToLowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }

    std::string FormatCount(uint64_t value)
    {
        return InsertThousandsSeparators(std::to_string(value));
    }

    std::string FormatMs(double nanoseconds)
    {
        const double ms = nanoseconds / 1'000'000.0;
        const double magnitude = std::abs(ms);
        if (magnitude == 0.0) {
            return "0";
        }
        if (magnitude >= 100.0) {
            return InsertThousandsSeparators(std::format("{:.1f}", ms));
        }
        if (magnitude >= 1.0) {
            return std::format("{:.2f}", ms);
        }
        if (magnitude >= 0.01) {
            return std::format("{:.3f}", ms);
        }
        return std::format("{:.4f}", ms);
    }

    std::string FormatClock(int64_t nanoseconds)
    {
        const int64_t totalSeconds = std::max<int64_t>(0, nanoseconds / 1'000'000'000);
        return std::format("{}:{:02}", totalSeconds / 60, totalSeconds % 60);
    }

    std::string_view FileName(std::string_view path)
    {
        const size_t slash = path.find_last_of("\\/");
        return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
    }

    std::string ToUtf8(std::wstring_view text)
    {
        if (text.empty()) {
            return {};
        }
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(size), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring ToWide(std::string_view text)
    {
        if (text.empty()) {
            return {};
        }
        const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring result(static_cast<size_t>(size), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
        return result;
    }

    bool ContainsIgnoreCase(std::string_view text, std::string_view term)
    {
        if (term.empty()) {
            return true;
        }
        const auto it = std::search(text.begin(), text.end(), term.begin(), term.end(),
            [](char a, char b) { return ToLowerAscii(a) == ToLowerAscii(b); });
        return it != text.end();
    }
}
