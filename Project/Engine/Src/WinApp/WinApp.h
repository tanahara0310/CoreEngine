#pragma once

#include <Windows.h>
#include <cstdint>
#include <functional>

#ifdef _DEBUG
#include <imgui_impl_win32.h>

// ImGuiのウィンドウプロシージャ（グローバル名前空間）
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // _DEBUG

namespace CoreEngine
{

    class WinApp {
    public:
        // ウィンドウの大きさ（スタティック定数）
        static const int32_t kClientWidth = 1280;
        static const int32_t kClientHeight = 720;

        /// @brief 現在のクライアント領域の幅を静的に取得
        static int32_t GetCurrentClientWidthStatic() { return currentClientWidthStatic_; }

        /// @brief 現在のクライアント領域の高さを静的に取得
        static int32_t GetCurrentClientHeightStatic() { return currentClientHeightStatic_; }

        /// @brief 指定された幅、高さ、タイトルで初期化
        /// @param width ウィンドウの幅
        /// @param height ウィンドウの高さ
        /// @param title ウィンドウのタイトル
        void Initialize(int32_t width, int32_t height, const wchar_t* title);

        /// @brief メッセージ処理
        /// @return 終了ならtrue
        bool ProcessMessage();

        /// @brief ウィンドウハンドルの取得
        /// @return hwnd
        HWND GetHwnd() const { return hwnd_; }

        /// @brief アプリケーションウィンドウを閉じる
        void CloseAppWindow();

        /// @brief hInstanceの取得
        /// @return hInstance
        HINSTANCE GetInstance() const { return wc_.hInstance; }

        /// @brief 現在のクライアント領域の幅を取得
        /// @return クライアント幅
        int32_t GetClientWidth() const { return currentClientWidth_; }

        /// @brief 現在のクライアント領域の高さを取得
        /// @return クライアント高さ
        int32_t GetClientHeight() const { return currentClientHeight_; }

        /// @brief ウィンドウサイズ変更時のコールバックを設定
        /// @param callback コールバック関数
        void SetResizeCallback(std::function<void(int32_t, int32_t)> callback) { resizeCallback_ = callback; }

    private:
        // ウィンドウプロシージャ
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

        // ウィンドウクラスの登録
        void RegisterWindowClass();

        // ウィンドウの生成
        void CreateAppWindow(const wchar_t* title);

        // ウィンドウハンドル
        HWND hwnd_;
        // ウィンドウクラス
        WNDCLASSEX wc_;

        // 現在のクライアント領域のサイズ
        int32_t currentClientWidth_ = kClientWidth;
        int32_t currentClientHeight_ = kClientHeight;

        // リサイズコールバック
        std::function<void(int32_t, int32_t)> resizeCallback_;

        // WinAppインスタンスへのポインタ（WindowProcから参照するため）
        static WinApp* instance_;

        // 現在のクライアント領域のサイズ（静的アクセス用）
        static int32_t currentClientWidthStatic_;
        static int32_t currentClientHeightStatic_;
    };
};
