#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief エンジンウィンドウのスクリーンキャプチャ機能
    /// メニューバーからキャプチャを実行し、ユーザーが指定したフォルダへPNG保存する
    class ScreenCapture {
    public:
        /// @brief ウィンドウハンドルを設定
        /// @param hwnd キャプチャ対象のウィンドウハンドル
        void SetHwnd(HWND hwnd) { hwnd_ = hwnd; }

        /// @brief キャプチャをリクエストする（次フレームで実行される）
        void RequestCapture() { captureRequested_ = true; }

        /// @brief 保留中のキャプチャがあれば実行する
        /// 毎フレームの描画前に呼び出すこと
        void ProcessPendingCapture();

    private:
        /// @brief ウィンドウ全体をキャプチャしてHBITMAPを返す
        /// @return キャプチャされたビットマップ（呼び出し側でDeleteObjectすること）
        HBITMAP CaptureWindow();

        /// @brief HBITMAPをPNGファイルとして保存する
        /// @param hBitmap 保存するビットマップ
        /// @param filePath 保存先ファイルパス
        /// @return 成功した場合true
        bool SaveBitmapAsPng(HBITMAP hBitmap, const std::wstring& filePath);

        /// @brief 保存先ファイルパスをユーザーに選択させる（Win32 GetSaveFileName使用）
        /// @param outPath 選択されたファイルパス（出力）
        /// @return ユーザーがパスを選択した場合true
        bool ShowSaveDialog(std::wstring& outPath);

    private:
        HWND hwnd_ = nullptr;
        bool captureRequested_ = false;
    };
}
