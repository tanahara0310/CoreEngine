#include "pch.h"
#include "Editor/External/ProfilerLauncher.h"

#include "Utility/Logger/Logger.h"

#include <ShObjIdl.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <string>

namespace CoreEngine::Editor
{
    namespace
    {
        constexpr wchar_t kProfilerFileName[] = L"CoreEngineProfiler.exe";

        /// @brief エディタがつなぎを待っている先（Tracy の既定のポート）
        constexpr wchar_t kEditorEndpoint[] = L"127.0.0.1:8086";

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

        /// @brief プロファイラーを引数つきで起動する。exe が無いか起動できなければ警告を出して false
        bool LaunchProfiler(const std::wstring& arguments)
        {
            const std::filesystem::path exe = ProfilerExecutablePath();
            std::error_code error;
            if (exe.empty() || !std::filesystem::is_regular_file(exe, error)) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "CoreEngine Profiler が見つかりません（CoreEngineProfiler をビルドしてください）: {}",
                    Logger::GetInstance().PathToUtf8(exe));
                return false;
            }
            const std::wstring directory = exe.parent_path().wstring();
            const INT_PTR result = reinterpret_cast<INT_PTR>(
                ::ShellExecuteW(nullptr, L"open", exe.c_str(), arguments.c_str(), directory.c_str(), SW_SHOWNORMAL));
            if (result <= 32) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "CoreEngine Profiler を起動できませんでした: {}", Logger::GetInstance().PathToUtf8(exe));
                return false;
            }
            return true;
        }

        /// @brief .tracy を選ぶ窓を出す（選ばれなければ空）
        std::filesystem::path PickTraceFile(HWND owner)
        {
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
            if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                return {};
            }
            const COMDLG_FILTERSPEC filters[] = {
                { L"Tracy の記録 (*.tracy)", L"*.tracy" },
                { L"すべてのファイル (*.*)", L"*.*" },
            };
            dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
            dialog->SetDefaultExtension(L"tracy");
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
            dialog->SetTitle(L"プロファイラーで開く記録");
            if (FAILED(dialog->Show(owner))) {
                return {};
            }
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (FAILED(dialog->GetResult(&item))) {
                return {};
            }
            PWSTR path = nullptr;
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
                return {};
            }
            std::filesystem::path result(path);
            ::CoTaskMemFree(path);
            return result;
        }
    }

    std::filesystem::path ProfilerExecutablePath()
    {
        std::array<wchar_t, MAX_PATH * 4> buffer{};
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return {};
        }
        return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path() / kProfilerFileName;
    }

    bool OpenProfiler()
    {
        return LaunchProfiler(std::wstring(L"--connect ") + kEditorEndpoint);
    }

    bool OpenTraceInProfiler(const std::filesystem::path& trace)
    {
        return LaunchProfiler(L"--open " + Quote(trace.wstring()));
    }

    void TraceFilePicker::Open(HWND owner)
    {
        if (picking_.valid()) {
            return;
        }
        // 選ぶ窓は、COM をシングルスレッドとして用意した別のスレッドで出す
        picking_ = std::async(std::launch::async, [owner]() {
            const HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            std::filesystem::path picked = PickTraceFile(owner);
            if (SUCCEEDED(result)) {
                ::CoUninitialize();
            }
            return picked;
            });
    }

    void TraceFilePicker::Update()
    {
        if (!picking_.valid() || picking_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        const std::filesystem::path picked = picking_.get();
        if (!picked.empty()) {
            OpenTraceInProfiler(picked);
        }
    }
}
