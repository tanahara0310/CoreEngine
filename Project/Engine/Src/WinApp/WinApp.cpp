#include "pch.h"
#include "WinApp.h"

#pragma comment(lib, "winmm.lib")

// 静的メンバの初期化

namespace CoreEngine
{
WinApp* WinApp::instance_ = nullptr;
int32_t WinApp::currentClientWidthStatic_ = WinApp::kClientWidth;
int32_t WinApp::currentClientHeightStatic_ = WinApp::kClientHeight;

void WinApp::Initialize(int32_t width, int32_t height, const wchar_t* title)
{
    // DPIアウェアネスを設定（ウィンドウ作成前に必要）
    // これにより、Windowsのビットマップスケーリングを回避し、物理ピクセルで正確に描画される
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    timeBeginPeriod(1); // タイマー精度を1msに設定
    hwnd_ = nullptr;
    instance_ = this; // インスタンスポインタを設定
    currentClientWidth_ = width;
    currentClientHeight_ = height;
    currentClientWidthStatic_ = width;
    currentClientHeightStatic_ = height;
    RegisterWindowClass();
    CreateAppWindow(title);
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
void WinApp::CreateAppWindow(const wchar_t* title)
{
    // 1280x720を基準サイズにした通常ウィンドウ（起動時に最大化表示）
    UINT style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

    RECT windowRect = { 0, 0, currentClientWidth_, currentClientHeight_ };
    AdjustWindowRect(&windowRect, style, FALSE);

    // ウィンドウの生成
    hwnd_ = CreateWindowEx(
        0,                              // 拡張ウィンドウスタイル
        wc_.lpszClassName,             // ウィンドウクラス名
        title,                         // タイトルバーの文字列
        style, // ウィンドウスタイル
        CW_USEDEFAULT,                 // ウィンドウのX座標
        CW_USEDEFAULT,                 // ウィンドウのY座標
        windowRect.right - windowRect.left,   // ウィンドウの横幅
        windowRect.bottom - windowRect.top,   // ウィンドウの縦幅
        nullptr,                       // 親ウィンドウのハンドル
        nullptr,                       // メニューハンドル
        wc_.hInstance,                 // インスタンスハンドル
        nullptr);                      // その他のパラメータ

    // 起動時はボーダーレス全画面。ただし**この時点では表示しない**。
    // SW_SHOWMAXIMIZED（最大化）ではタイトルバーが残り、タスクバーの分だけ
    // 作業領域が削られるため「全画面」にはならない。
    //
    // ここで全画面のジオメトリだけ先に適用しておくのは、クライアントサイズを
    // 確定させるため。これを表示時まで遅らせると、スワップチェーンと全レンダー
    // ターゲットを 1280x720 で作ったあと表示直後にモニタ解像度で作り直すことになる。
    // 実際の表示は起動シーケンス完了後の ShowMainWindow() で行う。
    SetFullscreen(true);
}

void WinApp::ShowMainWindow()
{
    if (!hwnd_ || isMainWindowShown_) {
        return;
    }

    // このフラグを立ててから ShowWindow する。以降の SetFullscreen（Alt+Enter）は
    // 通常どおり ShowWindow を伴う
    isMainWindowShown_ = true;

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void WinApp::SetFullscreen(bool fullscreen)
{
    if (!hwnd_ || isFullscreen_ == fullscreen) {
        return;
    }

    if (fullscreen) {
        // 戻り先（スタイルと配置）を退避
        windowedStyle_ = GetWindowLongPtr(hwnd_, GWL_STYLE);
        windowedPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd_, &windowedPlacement_);

        // 起動時（非表示のまま全画面化する経路）では showCmd が SW_HIDE になる。
        // そのまま復元すると Alt+Enter でウィンドウモードへ戻した瞬間に消えるので、
        // 従来と同じ「最大化ウィンドウ」へ矯正しておく
        if (windowedPlacement_.showCmd == SW_HIDE) {
            windowedPlacement_.showCmd = SW_SHOWMAXIMIZED;
        }

        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfo(monitor, &monitorInfo)) {
            return;
        }
        const RECT& monitorRect = monitorInfo.rcMonitor;

        // 枠と各種ボタンを外す（タイトルバーが消える）
        SetWindowLongPtr(hwnd_, GWL_STYLE,
            (windowedStyle_ & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU))
            | WS_POPUP);

        // rcWorkArea ではなく rcMonitor を使うこと。
        // rcWorkArea はタスクバーを除いた領域なので、そこに合わせるとタスクバーが残る。
        SetWindowPos(hwnd_, HWND_TOP,
            monitorRect.left, monitorRect.top,
            monitorRect.right - monitorRect.left,
            monitorRect.bottom - monitorRect.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

        // 起動シーケンス中（ShowMainWindow 前）はここで表示してはいけない。
        // 表示した瞬間から「メッセージを処理しない全画面ウィンドウ」になる
        if (isMainWindowShown_) {
            ShowWindow(hwnd_, SW_SHOW);
        }
        isFullscreen_ = true;
    } else {
        SetWindowLongPtr(hwnd_, GWL_STYLE, windowedStyle_);
        SetWindowPlacement(hwnd_, &windowedPlacement_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        isFullscreen_ = false;
    }
}

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{

#ifdef USE_IMGUI
    // ImGuiの処理を優先する
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

#endif // USE_IMGUI

    // メッセージに応じて固有の処理を行う
    switch (msg) {
        // Alt+Enter で全画面 / 通常ウィンドウを切り替える
    case WM_SYSKEYDOWN:
        if (wparam == VK_RETURN && instance_ != nullptr) {
            constexpr LPARAM kAltDownBit = 1LL << 29;
            if (lparam & kAltDownBit) {
                instance_->ToggleFullscreen();
                return 0;
            }
        }
        break;

        // Esc でアプリケーションを終了する
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
#ifdef USE_IMGUI
            // 名前入力などの最中に消えてしまわないよう、
            // ImGui がテキスト入力を受け取っている間は無視する
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) {
                break;
            }
#endif
            // × ボタンと同じ経路を通す
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

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
                currentClientWidthStatic_ = newWidth;
                currentClientHeightStatic_ = newHeight;

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
    MSG msg{};
    // キューが空になるまで全メッセージを処理（1件ずつだと入力がキューに滞留し遅延する）
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        // ウィンドウが破棄されたら終了
        if (msg.message == WM_QUIT) {
            return true;
        }
        // メッセージを処理
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return false;
}

void WinApp::RequestQuit()
{
    if (hwnd_) {
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    }
}

// ウィンドウの破棄
void WinApp::CloseAppWindow()
{
    CloseWindow(hwnd_);
    UnregisterClass(wc_.lpszClassName, wc_.hInstance);
    instance_ = nullptr; // インスタンスポインタをクリア
}
}
