#include "pch.h"
#include "Utility/File/FolderChangeWatch.h"

namespace CoreEngine
{
    namespace
    {
        constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
            | FILE_NOTIFY_CHANGE_SIZE;
    }

    FolderChangeWatch::~FolderChangeWatch()
    {
        Stop();
    }

    bool FolderChangeWatch::Start(const std::filesystem::path& folder)
    {
        Stop();

        const HANDLE handle = ::FindFirstChangeNotificationW(folder.c_str(), FALSE, kNotifyFilter);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        handle_ = handle;
        folder_ = folder;
        return true;
    }

    void FolderChangeWatch::Stop()
    {
        if (handle_) {
            ::FindCloseChangeNotification(static_cast<HANDLE>(handle_));
            handle_ = nullptr;
        }
        folder_.clear();
    }

    bool FolderChangeWatch::TakeChanged()
    {
        if (!handle_) {
            return false;
        }

        const HANDLE handle = static_cast<HANDLE>(handle_);
        const DWORD result = ::WaitForSingleObject(handle, 0);
        if (result == WAIT_TIMEOUT) {
            return false;
        }

        // 次の変更を受ける形に戻してから返す（戻した後の変更は次の呼び出しで知らせる）
        if (result != WAIT_OBJECT_0 || !::FindNextChangeNotification(handle)) {
            Stop();
        }
        return true;
    }
}
