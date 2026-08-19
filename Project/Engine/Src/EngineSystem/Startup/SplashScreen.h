#pragma once
#include <Windows.h>
#include <string>

namespace CoreEngine
{
    /// @brief 起動中に出すローディング画面（GDI 描画）
    /// @details D3D12 デバイス作成より前から表示したいので、あえて GDI で描く。
    ///          メインウィンドウは起動シーケンス完了まで非表示にし、このウィンドウだけがメッセージを処理する。
    class SplashScreen {
    public:
        SplashScreen() = default;
        ~SplashScreen();

        SplashScreen(const SplashScreen&) = delete;
        SplashScreen& operator=(const SplashScreen&) = delete;

        /// @brief スプラッシュを表示する
        /// @param hInstance インスタンスハンドル
        /// @param appName   見出しに出すアプリ名
        void Show(HINSTANCE hInstance, const std::wstring& appName);

        /// @brief 進捗と現在のステップ名を設定する
        /// @param progress 0.0〜1.0
        /// @param label    ステップ名（UTF-8）
        void SetStatus(float progress, const std::string& label);

        /// @brief ステップ内の細かい進行内容を設定する（UTF-8。空文字で消える）
        void SetDetail(const std::string& detail);

        /// @brief 溜まったウィンドウメッセージを処理し、必要なら再描画する
        /// @param forceRedraw true なら間引きを無視して即再描画する
        /// @note hwnd 指定の PeekMessage でも、スレッド宛の「送信済みメッセージ」は
        ///       処理される。これが Windows のハング検出への応答になるので、
        ///       重いステップの内側からも定期的に呼ぶこと。
        void Pump(bool forceRedraw = false);

        /// @brief スプラッシュを閉じる
        void Close();

        bool IsVisible() const { return hwnd_ != nullptr; }

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        /// @brief メモリ DC に組み立ててから一度に転送する（ちらつき防止）
        void Render(HDC targetDC);
        void Repaint();
        void DestroyFonts();

        HWND hwnd_ = nullptr;
        HINSTANCE hInstance_ = nullptr;

        HFONT titleFont_ = nullptr;
        HFONT labelFont_ = nullptr;
        HFONT detailFont_ = nullptr;

        std::wstring appName_;
        std::wstring label_;
        std::wstring detail_;
        float progress_ = 0.0f;

        int32_t width_ = 0;
        int32_t height_ = 0;
        uint32_t dpi_ = 96;

        ULONGLONG lastPaintTick_ = 0;
    };
}
