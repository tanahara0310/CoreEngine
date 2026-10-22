#include "pch.h"
#include "Utility/CommandLine/CommandLine.h"

#include <Windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace CoreEngine::CommandLine
{
    bool HasOption(std::wstring_view option)
    {
        int count = 0;
        LPWSTR* const args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
        if (!args) {
            return false;
        }
        bool found = false;
        for (int i = 1; i < count; ++i) {
            if (std::wstring_view(args[i]) == option) {
                found = true;
                break;
            }
        }
        ::LocalFree(args);
        return found;
    }

    std::wstring GetValue(std::wstring_view option)
    {
        int count = 0;
        LPWSTR* const args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
        if (!args) {
            return {};
        }
        std::wstring value;
        for (int i = 1; i + 1 < count; ++i) {
            if (std::wstring_view(args[i]) == option) {
                value = args[i + 1];
                break;
            }
        }
        ::LocalFree(args);
        return value;
    }
}
