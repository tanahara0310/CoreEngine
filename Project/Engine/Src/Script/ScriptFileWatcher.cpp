#include "pch.h"
#include "Script/ScriptFileWatcher.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <system_error>

namespace CoreEngine
{
    namespace
    {
        constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
            | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
        constexpr std::size_t kBufferSize = 64 * 1024;

        /// @brief スクリプトのファイルか（大文字小文字は区別しない）
        bool IsScriptFile(const std::filesystem::path& path)
        {
            std::wstring extension = path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return extension == L".as";
        }

        std::string ToUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }
    }

    ScriptFileWatcher::~ScriptFileWatcher()
    {
        Stop();
    }

    bool ScriptFileWatcher::Start(const std::filesystem::path& root)
    {
        Stop();

        Logger& logger = Logger::GetInstance();
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            logger.Logf(LogLevel::Warn, LogCategory::Script,
                "スクリプトのフォルダが無いので見張れません: {}", ToUtf8(root));
            return false;
        }

        const HANDLE directory = ::CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (directory == INVALID_HANDLE_VALUE) {
            logger.Logf(LogLevel::Warn, LogCategory::Script,
                "スクリプトのフォルダを開けないので見張れません（{}）: {}", ::GetLastError(), ToUtf8(root));
            return false;
        }

        const HANDLE stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent) {
            logger.Logf(LogLevel::Warn, LogCategory::Script,
                "見張りを終えるためのイベントを作れませんでした（{}）", ::GetLastError());
            ::CloseHandle(directory);
            return false;
        }

        handle_ = directory;
        stopEvent_ = stopEvent;
        root_ = root;
        thread_ = std::thread([this] { Run(); });
        return true;
    }

    void ScriptFileWatcher::Stop()
    {
        if (stopEvent_) {
            ::SetEvent(static_cast<HANDLE>(stopEvent_));
        }
        if (handle_) {
            ::CancelIoEx(static_cast<HANDLE>(handle_), nullptr);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (handle_) {
            ::CloseHandle(static_cast<HANDLE>(handle_));
            handle_ = nullptr;
        }
        if (stopEvent_) {
            ::CloseHandle(static_cast<HANDLE>(stopEvent_));
            stopEvent_ = nullptr;
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        changed_.clear();
    }

    bool ScriptFileWatcher::TakeSettledChanges(std::chrono::milliseconds settleTime,
                                               std::vector<std::filesystem::path>& changedFiles)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (changed_.empty() || std::chrono::steady_clock::now() - lastChange_ < settleTime) {
            return false;
        }
        changedFiles = std::move(changed_);
        changed_.clear();
        return true;
    }

    void ScriptFileWatcher::Record(std::filesystem::path path)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        lastChange_ = std::chrono::steady_clock::now();
        if (std::find(changed_.begin(), changed_.end(), path) == changed_.end()) {
            changed_.push_back(std::move(path));
        }
    }

    void ScriptFileWatcher::Run()
    {
        const HANDLE directory = static_cast<HANDLE>(handle_);
        const HANDLE stopEvent = static_cast<HANDLE>(stopEvent_);
        const HANDLE readEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!readEvent) {
            return;
        }

        std::vector<std::byte> buffer(kBufferSize);
        OVERLAPPED overlapped{};
        overlapped.hEvent = readEvent;

        for (;;) {
            ::ResetEvent(readEvent);
            DWORD ignored = 0;
            if (!::ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                    kNotifyFilter, &ignored, &overlapped, nullptr)) {
                break;
            }

            const HANDLE waited[] = { readEvent, stopEvent };
            if (::WaitForMultipleObjects(2, waited, FALSE, INFINITE) != WAIT_OBJECT_0) {
                // 終わりの合図。読み取りを取り消し、終わるのを待ってからイベントを閉じる
                ::CancelIoEx(directory, &overlapped);
                DWORD cancelled = 0;
                ::GetOverlappedResult(directory, &overlapped, &cancelled, TRUE);
                break;
            }

            DWORD transferred = 0;
            if (!::GetOverlappedResult(directory, &overlapped, &transferred, FALSE)) {
                break;
            }
            if (transferred == 0) {
                // 溜めきれずに取りこぼしたときは、フォルダごと変わったものとして渡す
                Record(root_);
                continue;
            }

            const std::byte* cursor = buffer.data();
            for (;;) {
                const auto* const info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
                const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                const std::filesystem::path path = root_ / name;
                if (IsScriptFile(path)) {
                    Record(path);
                }
                if (info->NextEntryOffset == 0) {
                    break;
                }
                cursor += info->NextEntryOffset;
            }
        }

        ::CloseHandle(readEvent);
    }
}
