#include "pch.h"
#include "PixCapture.h"
#include "Utility/Logger/Logger.h"

#include <chrono>
#include <filesystem>
#include <shlobj.h>

#ifdef USE_PIX
#include <pix3.h>
#endif

namespace CoreEngine
{
    bool PixCapture::runtimeLoaded_ = false;

    bool PixCapture::LoadPixRuntime()
    {
#ifdef USE_PIX
        // 既にロード済みならスキップ
        if (GetModuleHandleW(L"WinPixGpuCapturer.dll") != nullptr) {
            runtimeLoaded_ = true;
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "{}", "PixCapture: WinPixGpuCapturer.dll は既にロード済みです");
            return true;
        }

        // PIX インストールフォルダから最新の WinPixGpuCapturer.dll を検索してロード
        LPWSTR programFilesPath = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &programFilesPath))) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "PixCapture: Program Files フォルダの取得に失敗しました");
            return false;
        }

        std::wstring pixSearchPath = programFilesPath;
        CoTaskMemFree(programFilesPath);
        pixSearchPath += L"\\Microsoft PIX\\*";

        // PIX インストールフォルダ内で最新バージョンを探す
        WIN32_FIND_DATAW findData{};
        HANDLE hFind = FindFirstFileW(pixSearchPath.c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "PixCapture: PIX がインストールされていません");
            return false;
        }

        std::wstring latestVersionDir;
        do {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && findData.cFileName[0] != L'.') {
                std::wstring candidate = findData.cFileName;
                if (candidate > latestVersionDir) {
                    latestVersionDir = candidate;
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);

        if (latestVersionDir.empty()) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "PixCapture: PIX のバージョンフォルダが見つかりません");
            return false;
        }

        // DLL パスを構築
        std::wstring dllPath = pixSearchPath.substr(0, pixSearchPath.size() - 1);
        dllPath += latestVersionDir;
        dllPath += L"\\WinPixGpuCapturer.dll";

        // DLL をロード
        HMODULE hModule = LoadLibraryW(dllPath.c_str());
        if (!hModule) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "PixCapture: WinPixGpuCapturer.dll のロードに失敗しました");
            return false;
        }

        runtimeLoaded_ = true;
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "{}", "PixCapture: WinPixGpuCapturer.dll をロードしました");

        // PIX の HUD オーバーレイ（左上のカウンタ）を非表示にする
        DisableHUD(hModule);

        return true;
#else
        return false;
#endif
    }

    void PixCapture::ProcessPendingCapture()
    {
        if (!captureRequested_) {
            return;
        }
        captureRequested_ = false;

#ifdef USE_PIX
        if (!runtimeLoaded_) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "PixCapture: PIX ランタイムがロードされていません");
            return;
        }

        // キャプチャファイルパスを生成
        std::wstring filePath = GenerateCaptureFilePath();

        // Captures フォルダが存在しない場合は作成
        std::filesystem::path outputDir = std::filesystem::path(filePath).parent_path();
        if (!outputDir.empty() && !std::filesystem::exists(outputDir)) {
            std::filesystem::create_directories(outputDir);
        }

        // 次の1フレームだけをキャプチャ
        HRESULT hr = PIXGpuCaptureNextFrames(filePath.c_str(), 1);
        if (SUCCEEDED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "{}", "PixCapture: GPU キャプチャをリクエストしました");
        } else {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}", "PixCapture: GPU キャプチャの開始に失敗しました");
        }
#else
        Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "PixCapture: USE_PIX が未定義です");
#endif
    }

    std::wstring PixCapture::GenerateCaptureFilePath() const
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &time);

        wchar_t fileName[MAX_PATH] = {};
        swprintf_s(fileName, L"PixCapture_%04d%02d%02d_%02d%02d%02d.wpix",
            localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
            localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

        std::wstring outputPath = L"Captures\\";
        outputPath += fileName;

        return outputPath;
    }

    void PixCapture::DisableHUD([[maybe_unused]] HMODULE gpuCapturerModule)
    {
#ifdef USE_PIX
        // PIX HUD オーバーレイを非表示にする
        // PIXSetHUDOptions は WinPixEventRuntime（NuGet リンク済み）から提供される
        // DLL によって関数名が異なるため、複数のアプローチを試みる
        using PFN_SetHUDOptions = void(WINAPI*)(UINT);
        constexpr UINT kHideHUD = 0x4; // PIX_HUD_SHOW_ON_NO_WINDOWS

        // 検索対象: GpuCapturer → EventRuntime (x64) → EventRuntime (通常)
        const wchar_t* dllNames[] = {
            L"WinPixGpuCapturer.dll",
            L"WinPixEventRuntime_UAP.dll",
            L"WinPixEventRuntime.dll",
        };

        // 複数の関数名を試みる（バージョンによって異なる場合がある）
        const char* funcNames[] = {
            "PIXSetHUDOptions",
            "SetHUDOptions",
        };

        for (const auto* dll : dllNames) {
            HMODULE hMod = GetModuleHandleW(dll);
            if (!hMod) continue;

            for (const auto* func : funcNames) {
                auto fn = reinterpret_cast<PFN_SetHUDOptions>(GetProcAddress(hMod, func));
                if (fn) {
                    fn(kHideHUD);
                    Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "{}", "PixCapture: PIX HUD を非表示にしました");
                    return;
                }
            }
        }

        // GetProcAddress で見つからない場合、環境変数で制御を試みる
        // 一部の PIX バージョンはこの環境変数を参照する
        SetEnvironmentVariableW(L"PIX_HUD_OPTIONS", L"4");
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "{}", "PixCapture: PIX HUD 非表示を環境変数で設定しました");
#endif
    }
}
