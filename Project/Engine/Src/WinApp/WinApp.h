#pragma once

#include <Windows.h>
#include <cstdint>
#include <functional>

#ifdef USE_IMGUI
#include <imgui.h>
#include <imgui_impl_win32.h>

// ImGuiのウィンドウプロシージャ（グローバル名前空間）
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // USE_IMGUI

namespace CoreEngine
{

    /// @brief メインウィンドウの生成・表示・メッセージ処理を担う
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
        /// @note ここではウィンドウを表示しない。表示は起動シーケンス完了後の ShowMainWindow()
        void Initialize(int32_t width, int32_t height, const wchar_t* title);

        /// @brief メインウィンドウを表示する
        /// @details 生成時はあえて非表示のままにしてある。表示してから初期化を続けると、
        ///          メッセージポンプが回らない全画面ウィンドウが「応答なし」と判定されるため。
        void ShowMainWindow();

        /// @brief メッセージ処理
        /// @return 終了ならtrue
        bool ProcessMessage();

        /// @brief ウィンドウハンドルの取得
        /// @return hwnd
        HWND GetHwnd() const { return hwnd_; }

        /// @brief アプリケーションウィンドウを閉じる
        void CloseAppWindow();

        /// @brief アプリケーションの終了を要求する
        /// @details タイトルバーの × と同じ経路（WM_CLOSE → WM_DESTROY → PostQuitMessage）を通すので、
        ///          終了処理の順序が通常終了時と完全に一致する。
        void RequestQuit();

        /// @brief 全画面表示（ボーダーレス）と通常ウィンドウを切り替える
        /// @details 最大化（SW_SHOWMAXIMIZED）ではタイトルバーが残り、タスクバーも作業領域として
        ///          残ってしまう。全画面にするには枠を外し（WS_POPUP）、モニタ矩形と
        ///          完全に一致させたうえで最前面へ出す必要がある。
        /// @param fullscreen 全画面にするなら true
        void SetFullscreen(bool fullscreen);

        /// @brief 全画面表示を切り替える（Alt+Enter と同じ）
        void ToggleFullscreen() { SetFullscreen(!isFullscreen_); }

        /// @brief 全画面表示中かどうか
        bool IsFullscreen() const { return isFullscreen_; }

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

        // メインウィンドウを ShowMainWindow() で表示済みか。
        // 起動シーケンス中は false で、この間 SetFullscreen は ShowWindow を呼ばない
        bool isMainWindowShown_ = false;

        // 全画面表示の状態と、通常ウィンドウへ戻すための退避情報
        bool isFullscreen_ = false;
        WINDOWPLACEMENT windowedPlacement_ = { sizeof(WINDOWPLACEMENT) };
        LONG_PTR windowedStyle_ = 0;

        // WinAppインスタンスへのポインタ（WindowProcから参照するため）
        static WinApp* instance_;

        // 現在のクライアント領域のサイズ（静的アクセス用）
        static int32_t currentClientWidthStatic_;
        static int32_t currentClientHeightStatic_;
    };
};
