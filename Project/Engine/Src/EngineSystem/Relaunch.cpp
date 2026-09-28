#include "pch.h"
#include "EngineSystem/Relaunch.h"

#include <Windows.h>
#include <shellapi.h>

#include <optional>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace CoreEngine
{
    namespace
    {
        /// @brief 起動し直すときの引数（頼まれていなければ空）
        std::optional<std::wstring>& PendingArguments()
        {
            static std::optional<std::wstring> arguments;
            return arguments;
        }

        /// @brief CommandLineToArgvW が 1 つの引数として読むように " で囲む
        std::wstring Quote(const std::wstring& text)
        {
            // 末尾に並ぶ '\' は数を倍にしてから " で閉じる
            size_t trailing = 0;
            for (auto it = text.rbegin(); it != text.rend() && *it == L'\\'; ++it) {
                ++trailing;
            }
            return L"\"" + text + std::wstring(trailing, L'\\') + L"\"";
        }
    }

    void Relaunch::Request(std::wstring arguments)
    {
        PendingArguments() = std::move(arguments);
        ::PostQuitMessage(0);
    }

    void Relaunch::RequestProject(const std::filesystem::path& folder)
    {
        Request(L"--project " + Quote(folder.wstring()));
    }

    void Relaunch::RunIfRequested()
    {
        std::optional<std::wstring> arguments = std::move(PendingArguments());
        PendingArguments().reset();
        if (!arguments) {
            return;
        }

        wchar_t exePath[MAX_PATH] = {};
        ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        ::ShellExecuteW(nullptr, L"open", exePath, arguments->empty() ? nullptr : arguments->c_str(),
            nullptr, SW_SHOWDEFAULT);
    }
}
