#include "pch.h"
#include "CrashDump.h"
#include "CrashReport.h"
#include "EngineSystem/EngineVersion.h"
#include "Utility/Logger/Logger.h"
#include <atomic>
#include <filesystem>
#include <format>


namespace CoreEngine
{
namespace
{
#if defined(_DEBUG)
    constexpr const char* kBuildName = "Debug";
#elif defined(CORE_EDITOR)
    constexpr const char* kBuildName = "Development";
#else
    constexpr const char* kBuildName = "Release";
#endif

    // ダンプと記録を書くフォルダ（登録したときの作業フォルダの下の Dumps）
    std::wstring dumpFolder = L"Dumps";
    // 書いているスレッド（0 なら誰も書いていない）
    std::atomic<DWORD> writingThread{ 0 };

    struct CrashJob {
        EXCEPTION_POINTERS* exception;
        DWORD threadId;
    };

    std::string WideToUtf8(const std::wstring& text)
    {
        const int length = static_cast<int>(text.size());
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(size > 0 ? size : 0), '\0');
        if (size > 0) {
            ::WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
        }
        return result;
    }

    /// @brief 落ちたスレッドと例外の情報を入れたミニダンプを書く
    bool WriteMiniDump(const std::wstring& path, const CrashJob& job)
    {
        const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = job.threadId;
        information.ExceptionPointers = job.exception;
        information.ClientPointers = FALSE;
        const BOOL written = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, MiniDumpNormal,
            &information, nullptr, nullptr);
        ::CloseHandle(file);
        return written != FALSE;
    }

    /// @brief 行を UTF-8 のテキストファイルに書く
    void WriteTextFile(const std::wstring& path, const std::vector<std::string>& lines)
    {
        const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }
        for (const std::string& line : lines) {
            const std::string text = line + "\r\n";
            DWORD written = 0;
            ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        }
        ::CloseHandle(file);
    }

    /// @brief ダンプ・落ちたときの記録（.txt）・ログを書く
    void WriteCrashReport(const CrashJob& job)
    {
        SYSTEMTIME time{};
        ::GetLocalTime(&time);
        ::CreateDirectoryW(dumpFolder.c_str(), nullptr);
        const std::wstring stem = std::format(L"{}\\{:04}-{:02}-{:02}_{:02}{:02}{:02}", dumpFolder,
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
        const std::wstring dumpPath = stem + L".dmp";
        const std::wstring reportPath = stem + L".txt";
        const bool dumped = WriteMiniDump(dumpPath, job);

        std::vector<std::string> lines;
        lines.push_back(std::format("CoreEngine {}（{}）が落ちました: {:04}-{:02}-{:02} {:02}:{:02}:{:02}",
            kEngineVersion, kBuildName, time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond));
        try {
            lines.push_back("理由: " + CrashReport::DescribeException(*job.exception));
            lines.push_back(dumped ? "ダンプ: " + WideToUtf8(dumpPath) : std::string("ダンプ: 書けませんでした"));
            lines.push_back("記録: " + WideToUtf8(reportPath));
            lines.push_back("呼び出しの列（上が落ちた場所）:");
            for (const std::string& frame : CrashReport::DescribeCallStack(*job.exception)) {
                lines.push_back("  " + frame);
            }
        }
        catch (...) {
            lines.push_back("呼び出しの列を作れませんでした");
        }
        WriteTextFile(reportPath, lines);

        // ログのキューに積んでから、書き出させる
        Logger& log = Logger::GetInstance();
        for (const std::string& line : lines) {
            log.Log(line, LogLevel::Critical, LogCategory::System);
        }
        log.Flush();
    }

    DWORD WINAPI WriteCrashReportThread(void* parameter)
    {
        WriteCrashReport(*static_cast<const CrashJob*>(parameter));
        return 0;
    }
}

void CrashDump::Register()
{
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::absolute(L"Dumps", error);
    if (!error) {
        dumpFolder = folder.wstring();
    }
    ::SetUnhandledExceptionFilter(HandleException);
}

LONG WINAPI CrashDump::HandleException(EXCEPTION_POINTERS* exception)
{
    const DWORD threadId = ::GetCurrentThreadId();
    DWORD writing = 0;
    if (!writingThread.compare_exchange_strong(writing, threadId)) {
        // 書いている途中のスレッドがまた落ちたら、そのまま終わる
        if (writing == threadId) {
            return EXCEPTION_EXECUTE_HANDLER;
        }
        // ほかのスレッドが書いている間は、書き終えて終わるのを待つ
        ::Sleep(INFINITE);
    }

    CrashJob job{ exception, threadId };
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        // スタックがあふれたときは、別のスレッドで書く
        if (const HANDLE thread = ::CreateThread(nullptr, 0, WriteCrashReportThread, &job, 0, nullptr)) {
            ::WaitForSingleObject(thread, INFINITE);
            ::CloseHandle(thread);
        }
    }
    else {
        WriteCrashReport(job);
    }

    // プロセスを終える
    return EXCEPTION_EXECUTE_HANDLER;
}
}
