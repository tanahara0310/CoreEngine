#include "Platform/FileDialog.h"

#include <ShObjIdl.h>
#include <wrl/client.h>

#include <iterator>

namespace CoreEngine::Profiler::Platform
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        const COMDLG_FILTERSPEC kTraceFilters[] = {
            { L"Tracy の記録 (*.tracy)", L"*.tracy" },
            { L"すべてのファイル (*.*)", L"*.*" },
        };

        /// @brief 窓で選ばれたファイルのパスを取り出す
        std::optional<std::wstring> ResultPath(IFileDialog* dialog)
        {
            ComPtr<IShellItem> item;
            if (FAILED(dialog->GetResult(&item))) {
                return std::nullopt;
            }
            PWSTR path = nullptr;
            if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
                return std::nullopt;
            }
            std::wstring result = path;
            ::CoTaskMemFree(path);
            return result;
        }
    }

    std::optional<std::wstring> ShowOpenTraceDialog(HWND owner)
    {
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            return std::nullopt;
        }
        dialog->SetTitle(L"記録を開く");
        dialog->SetFileTypes(static_cast<UINT>(std::size(kTraceFilters)), kTraceFilters);
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        if (FAILED(dialog->Show(owner))) {
            return std::nullopt;
        }
        return ResultPath(dialog.Get());
    }

    std::optional<std::wstring> ShowSaveTraceDialog(HWND owner, const std::wstring& defaultName)
    {
        ComPtr<IFileSaveDialog> dialog;
        if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            return std::nullopt;
        }
        dialog->SetTitle(L"記録を保存");
        dialog->SetFileTypes(static_cast<UINT>(std::size(kTraceFilters)), kTraceFilters);
        dialog->SetDefaultExtension(L"tracy");
        dialog->SetFileName(defaultName.c_str());
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
        if (FAILED(dialog->Show(owner))) {
            return std::nullopt;
        }
        return ResultPath(dialog.Get());
    }
}
