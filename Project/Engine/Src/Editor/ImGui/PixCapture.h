#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>

namespace CoreEngine
{
    class GraphicsCore;

    /// @brief PIX (PIX on Windows) によるプログラム的 GPU キャプチャ機能
    /// USE_PIX 定義時に常時有効。ボタン押下時に次の1フレームだけをキャプチャする
    /// 非キャプチャ時のオーバーヘッドは関数ポインタ間接呼び出し程度で軽微
    class PixCapture {
    public:
        /// @brief PIX GPU キャプチャ DLL をロードする
        /// D3D12 デバイス作成前に呼び出す必要がある
        /// @return DLL のロードに成功した場合 true
        static bool LoadPixRuntime();

        /// @brief PIX が使用可能かチェック
        /// @return DLL がロード済みの場合 true
        static bool IsPixAvailable() { return runtimeLoaded_; }

        /// @brief PIX キャプチャをリクエストする
        /// 次のフレーム描画開始前に1フレーム分のキャプチャが実行される
        void RequestCapture() { captureRequested_ = true; }

        /// @brief リクエストがあればキャプチャを実行する
        /// ShowMainMenuBar の先頭で毎フレーム呼ぶ（フラグが立っていなければ即 return）
        void ProcessPendingCapture();

    private:
        /// @brief .wpix ファイルの保存先パスを生成
        std::wstring GenerateCaptureFilePath() const;

        /// @brief PIX HUD オーバーレイを非表示にする
        /// @param gpuCapturerModule ロード済みの WinPixGpuCapturer.dll ハンドル
        static void DisableHUD(HMODULE gpuCapturerModule);

    private:
        bool captureRequested_ = false;
        static bool runtimeLoaded_;
    };
}
