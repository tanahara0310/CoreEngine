#include "ScreenCapture.h"
#include "Utility/Logger/Logger.h"

#include <commdlg.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "comdlg32.lib")

namespace CoreEngine
{
    void ScreenCapture::ProcessPendingCapture()
    {
        if (!captureRequested_) {
            return;
        }
        captureRequested_ = false;

        if (!hwnd_) {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System, "{}", "ScreenCapture: HWNDが設定されていません");
            return;
        }

        // ウィンドウをキャプチャ（前フレームの描画結果がDCに残っている）
        HBITMAP hBitmap = CaptureWindow();
        if (!hBitmap) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}", "ScreenCapture: ウィンドウのキャプチャに失敗しました");
            return;
        }

        // 保存先をユーザーに選択させる
        std::wstring savePath;
        if (ShowSaveDialog(savePath)) {
            if (SaveBitmapAsPng(hBitmap, savePath)) {
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System, "{}", "ScreenCapture: スクリーンショットを保存しました");
            } else {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System, "{}", "ScreenCapture: PNG保存に失敗しました");
            }
        }

        DeleteObject(hBitmap);
    }

    HBITMAP ScreenCapture::CaptureWindow()
    {
        // クライアント領域のサイズを取得
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;

        if (width <= 0 || height <= 0) {
            return nullptr;
        }

        // ウィンドウDCを取得
        HDC hdcWindow = GetDC(hwnd_);
        if (!hdcWindow) {
            return nullptr;
        }

        // メモリDCとビットマップを作成
        HDC hdcMem = CreateCompatibleDC(hdcWindow);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, width, height);
        HGDIOBJ hOld = SelectObject(hdcMem, hBitmap);

        // ウィンドウの内容をコピー
        BitBlt(hdcMem, 0, 0, width, height, hdcWindow, 0, 0, SRCCOPY);

        // クリーンアップ
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd_, hdcWindow);

        return hBitmap;
    }

    bool ScreenCapture::SaveBitmapAsPng(HBITMAP hBitmap, const std::wstring& filePath)
    {
        using Microsoft::WRL::ComPtr;

        // ビットマップ情報を取得
        BITMAP bmp{};
        if (!GetObject(hBitmap, sizeof(BITMAP), &bmp)) {
            return false;
        }

        // WICファクトリを作成（IWICImagingFactoryはMTAでも動作する）
        ComPtr<IWICImagingFactory> wicFactory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) {
            return false;
        }

        // ファイルストリームを作成
        ComPtr<IWICStream> stream;
        hr = wicFactory->CreateStream(&stream);
        if (FAILED(hr)) {
            return false;
        }

        hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            return false;
        }

        // PNGエンコーダを作成
        ComPtr<IWICBitmapEncoder> encoder;
        hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr)) {
            return false;
        }

        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            return false;
        }

        // フレームを作成
        ComPtr<IWICBitmapFrameEncode> frame;
        hr = encoder->CreateNewFrame(&frame, nullptr);
        if (FAILED(hr)) {
            return false;
        }

        hr = frame->Initialize(nullptr);
        if (FAILED(hr)) {
            return false;
        }

        UINT width = static_cast<UINT>(bmp.bmWidth);
        UINT height = static_cast<UINT>(bmp.bmHeight);

        hr = frame->SetSize(width, height);
        if (FAILED(hr)) {
            return false;
        }

        WICPixelFormatGUID formatGuid = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&formatGuid);
        if (FAILED(hr)) {
            return false;
        }

        // ビットマップのピクセルデータを取得
        BITMAPINFOHEADER bi{};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bmp.bmWidth;
        bi.biHeight = -bmp.bmHeight; // トップダウン
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        UINT stride = width * 4;
        UINT bufferSize = stride * height;
        std::vector<BYTE> pixels(bufferSize);

        HDC hdc = GetDC(nullptr);
        GetDIBits(hdc, hBitmap, 0, height,
            pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        hr = frame->WritePixels(height, stride, bufferSize, pixels.data());
        if (FAILED(hr)) {
            return false;
        }

        hr = frame->Commit();
        if (FAILED(hr)) {
            return false;
        }

        hr = encoder->Commit();
        if (FAILED(hr)) {
            return false;
        }

        return true;
    }

    bool ScreenCapture::ShowSaveDialog(std::wstring& outPath)
    {
        // デフォルトファイル名をタイムスタンプから生成
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &time);

        wchar_t fileName[MAX_PATH] = {};
        swprintf_s(fileName, L"Screenshot_%04d%02d%02d_%02d%02d%02d.png",
            localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
            localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

        // Win32 GetSaveFileName（COMアパートメント非依存）
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"PNG\u753b\u50cf (*.png)\0*.png\0\u3059\u3079\u3066\u306e\u30d5\u30a1\u30a4\u30eb (*.*)\0*.*\0";
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"\u30b9\u30af\u30ea\u30fc\u30f3\u30b7\u30e7\u30c3\u30c8\u3092\u4fdd\u5b58";
        ofn.lpstrDefExt = L"png";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

        if (GetSaveFileNameW(&ofn)) {
            outPath = fileName;
            return true;
        }
        return false;
    }
}
