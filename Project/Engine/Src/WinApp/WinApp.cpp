#include "WinApp.h"

#pragma comment(lib, "winmm.lib")

// 静的メンバの初期化

namespace CoreEngine
{
WinApp* WinApp::instance_ = nullptr;

void WinApp::Initialize(int32_t width, int32_t height, const wchar_t* title)
{
    timeBeginPeriod(1); // タイマー精度を1msに設定
    hwnd_ = nullptr;
    instance_ = this; // インスタンスポインタを設定
    currentClientWidth_ = width;
    currentClientHeight_ = height;
    RegisterWindowClass();
    CreateAppWindow(title, width, height);
}

// ウィンドウクラスの登録
void WinApp::RegisterWindowClass()
{
    // ウィンドウクラスの定義
    wc_ = {};
    // ウィンドウクラスのサイズ
    wc_.cbSize = sizeof(WNDCLASSEX);
    // ウィンドウプロシージャ
    wc_.lpfnWndProc = WindowProc;
    // ウィンドウクラスの名前
    wc_.lpszClassName = L"CG2WindowClass";
    // インスタンスハンドル
    wc_.hInstance = GetModuleHandleA(nullptr);
    // カーソル
    wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // ウィンドウクラスの登録
    RegisterClassEx(&wc_);
}

// ウィンドウの生成
void WinApp::CreateAppWindow(const wchar_t* title, int32_t clientWidth, int32_t clientHeight)
{

    // ウィンドウサイズを表す構造体にクライアント領域を入れる
    RECT wrc = { 0, 0, clientWidth, clientHeight };

    UINT style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX /*| WS_MAXIMIZE*/;

    // クライアント領域を元に実際のサイズにwrcを変更
    AdjustWindowRect(&wrc, style, false);

    // ウィンドウの生成
    hwnd_ = CreateWindowEx(
        0,                              // 拡張ウィンドウスタイル
        wc_.lpszClassName,             // ウィンドウクラス名
        title,                         // タイトルバーの文字列
        style, // ウィンドウスタイル
        CW_USEDEFAULT,                 // ウィンドウのX座標
        CW_USEDEFAULT,                 // ウィンドウのY座標
        wrc.right - wrc.left,         // ウィンドウの横幅
        wrc.bottom - wrc.top,         // ウィンドウの縦幅
        nullptr,                       // 親ウィンドウのハンドル
        nullptr,                       // メニューハンドル
        wc_.hInstance,                 // インスタンスハンドル
        nullptr);                      // その他のパラメータ

    // ウィンドウを表示
    ShowWindow(hwnd_, SW_SHOW);
}

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{

#ifdef _DEBUG
    // ImGuiの処理を優先する
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

#endif // _DEBUG

    // メッセージに応じて固有の処理を行う
    switch (msg) {
        // ウィンドウが破棄された時
    case WM_DESTROY:
        // OSに対して、アプリの終了を伝える
        PostQuitMessage(0);
        return 0;

        // ウィンドウサイズが変更された時
    case WM_SIZE:
        if (instance_ != nullptr && wparam != SIZE_MINIMIZED) {
            // 新しいクライアント領域のサイズを取得
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            int32_t newWidth = clientRect.right - clientRect.left;
            int32_t newHeight = clientRect.bottom - clientRect.top;

            // サイズが実際に変更された場合のみコールバックを呼び出す
            if (newWidth > 0 && newHeight > 0 &&
                (newWidth != instance_->currentClientWidth_ || newHeight != instance_->currentClientHeight_)) {

                instance_->currentClientWidth_ = newWidth;
                instance_->currentClientHeight_ = newHeight;

                // コールバックが設定されていれば呼び出す
                if (instance_->resizeCallback_) {
                    instance_->resizeCallback_(newWidth, newHeight);
                }
            }
        }
        return 0;
    }

    // 標準のメッセージ処理を行う
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

// メッセージ処理
bool WinApp::ProcessMessage()
{
    MSG msg;
    // メッセージがある限りループ
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        // メッセージを処理
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ウィンドウが破棄されたら終了
    if (msg.message == WM_QUIT) {
        return true;
    }

    return false;
}

// ウィンドウの破棄
void WinApp::CloseAppWindow()
{
    CloseWindow(hwnd_);
    UnregisterClass(wc_.lpszClassName, wc_.hInstance);
    instance_ = nullptr; // インスタンスポインタをクリア
}
}
