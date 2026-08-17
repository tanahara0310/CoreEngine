#include "pch.h"
#include "SplashScreen.h"

#include <algorithm>

namespace CoreEngine
{
    namespace
    {
        constexpr const wchar_t* kSplashClassName = L"CoreEngineSplashClass";

        // 96 DPI 基準の論理サイズ。実 DPI に合わせて拡大する
        constexpr int32_t kBaseWidth = 560;
        constexpr int32_t kBaseHeight = 190;

        // 再描画の間引き間隔。StartupProgress::Tick はシェーダ 1 本ごとに来るので、
        // 毎回描くと GDI の分だけ起動が伸びる
        constexpr ULONGLONG kRepaintIntervalMs = 33;

        constexpr COLORREF kBackColor = RGB(26, 28, 33);
        constexpr COLORREF kPanelEdgeColor = RGB(58, 62, 72);
        constexpr COLORREF kTitleColor = RGB(236, 238, 242);
        constexpr COLORREF kLabelColor = RGB(198, 204, 214);
        constexpr COLORREF kDetailColor = RGB(126, 132, 144);
        constexpr COLORREF kTrackColor = RGB(44, 47, 55);
        constexpr COLORREF kFillColor = RGB(74, 156, 255);

        /// @brief UTF-8 を UTF-16 へ。GDI は wchar_t しか受け取らない
        std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty()) {
                return {};
            }
            const int length = MultiByteToWideChar(
                CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0) {
                return {};
            }
            std::wstring result(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(
                CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
            return result;
        }

        int32_t Scale(int32_t value, uint32_t dpi)
        {
            return MulDiv(value, static_cast<int32_t>(dpi), 96);
        }

        HFONT CreateUiFont(int32_t pointSize, int32_t weight, uint32_t dpi)
        {
            return CreateFontW(
                -MulDiv(pointSize, static_cast<int32_t>(dpi), 72), 0, 0, 0,
                weight, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Yu Gothic UI");
        }
    }

    SplashScreen::~SplashScreen()
    {
        Close();
    }

    void SplashScreen::Show(HINSTANCE hInstance, const std::wstring& appName)
    {
        if (hwnd_) {
            return;
        }

        hInstance_ = hInstance;
        appName_ = appName;

        static bool isClassRegistered = false;
        if (!isClassRegistered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = &SplashScreen::WindowProc;
            wc.hInstance = hInstance_;
            wc.lpszClassName = kSplashClassName;
            wc.hCursor = LoadCursor(nullptr, IDC_WAIT);
            // 背景は自前で塗る（WM_ERASEBKGND を潰してちらつきを消す）
            wc.hbrBackground = nullptr;
            RegisterClassExW(&wc);
            isClassRegistered = true;
        }

        // マウスカーソルのあるモニタの中央に出す（作業領域基準）
        POINT cursor{};
        GetCursorPos(&cursor);
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        RECT area{ 0, 0, 1280, 720 };
        if (GetMonitorInfo(monitor, &monitorInfo)) {
            area = monitorInfo.rcWork;
        }

        width_ = kBaseWidth;
        height_ = kBaseHeight;
        const int32_t x = area.left + ((area.right - area.left) - width_) / 2;
        const int32_t y = area.top + ((area.bottom - area.top) - height_) / 2;

        hwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kSplashClassName,
            L"",
            WS_POPUP,
            x, y, width_, height_,
            nullptr, nullptr, hInstance_, this);

        if (!hwnd_) {
            return;
        }

        // DPI はウィンドウを作ってからでないと確定しない（PER_MONITOR_AWARE_V2 のため
        // システム DPI とモニタ DPI が食い違うことがある）。実サイズへ作り直す
        dpi_ = GetDpiForWindow(hwnd_);
        if (dpi_ == 0) {
            dpi_ = 96;
        }
        if (dpi_ != 96) {
            width_ = Scale(kBaseWidth, dpi_);
            height_ = Scale(kBaseHeight, dpi_);
            SetWindowPos(hwnd_, HWND_TOPMOST,
                area.left + ((area.right - area.left) - width_) / 2,
                area.top + ((area.bottom - area.top) - height_) / 2,
                width_, height_, SWP_NOACTIVATE);
        }

        titleFont_ = CreateUiFont(15, FW_SEMIBOLD, dpi_);
        labelFont_ = CreateUiFont(11, FW_NORMAL, dpi_);
        detailFont_ = CreateUiFont(9, FW_NORMAL, dpi_);

        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        Repaint();
    }

    void SplashScreen::SetStatus(float progress, const std::string& label)
    {
        progress_ = std::clamp(progress, 0.0f, 1.0f);
        label_ = Utf8ToWide(label);
        // ステップが変わったら細目はいったん消す（前のステップの残骸を出さない）
        detail_.clear();
    }

    void SplashScreen::SetDetail(const std::string& detail)
    {
        detail_ = Utf8ToWide(detail);
    }

    void SplashScreen::Pump(bool forceRedraw)
    {
        if (!hwnd_) {
            return;
        }

        MSG msg{};
        while (PeekMessageW(&msg, hwnd_, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        const ULONGLONG now = GetTickCount64();
        if (forceRedraw || (now - lastPaintTick_) >= kRepaintIntervalMs) {
            lastPaintTick_ = now;
            Repaint();
        }
    }

    void SplashScreen::Close()
    {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        DestroyFonts();
    }

    void SplashScreen::DestroyFonts()
    {
        if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
        if (labelFont_) { DeleteObject(labelFont_); labelFont_ = nullptr; }
        if (detailFont_) { DeleteObject(detailFont_); detailFont_ = nullptr; }
    }

    void SplashScreen::Repaint()
    {
        if (!hwnd_) {
            return;
        }
        HDC dc = GetDC(hwnd_);
        if (!dc) {
            return;
        }
        Render(dc);
        ReleaseDC(hwnd_, dc);
    }

    void SplashScreen::Render(HDC targetDC)
    {
        const RECT full{ 0, 0, width_, height_ };

        // メモリ DC に一枚組み立ててから転送する（直接描くとちらつく）
        HDC memDC = CreateCompatibleDC(targetDC);
        if (!memDC) {
            return;
        }
        HBITMAP bitmap = CreateCompatibleBitmap(targetDC, width_, height_);
        if (!bitmap) {
            DeleteDC(memDC);
            return;
        }
        HGDIOBJ oldBitmap = SelectObject(memDC, bitmap);

        // 背景と外枠
        HBRUSH backBrush = CreateSolidBrush(kBackColor);
        FillRect(memDC, &full, backBrush);
        DeleteObject(backBrush);

        HBRUSH edgeBrush = CreateSolidBrush(kPanelEdgeColor);
        FrameRect(memDC, &full, edgeBrush);
        DeleteObject(edgeBrush);

        SetBkMode(memDC, TRANSPARENT);

        const int32_t margin = Scale(28, dpi_);
        const int32_t right = width_ - margin;

        // 見出し（アプリ名）
        RECT titleRect{ margin, Scale(26, dpi_), right, Scale(56, dpi_) };
        HGDIOBJ oldFont = SelectObject(memDC, titleFont_);
        SetTextColor(memDC, kTitleColor);
        DrawTextW(memDC, appName_.c_str(), -1, &titleRect,
            DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        // 現在のステップ名（左）とパーセント（右）
        RECT labelRect{ margin, Scale(78, dpi_), right, Scale(102, dpi_) };
        SelectObject(memDC, labelFont_);
        SetTextColor(memDC, kLabelColor);
        DrawTextW(memDC, label_.c_str(), -1, &labelRect,
            DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        const std::wstring percent = std::to_wstring(static_cast<int32_t>(progress_ * 100.0f + 0.5f)) + L"%";
        DrawTextW(memDC, percent.c_str(), -1, &labelRect,
            DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

        // プログレスバー
        const int32_t barTop = Scale(112, dpi_);
        const int32_t barHeight = Scale(8, dpi_);
        RECT track{ margin, barTop, right, barTop + barHeight };
        HBRUSH trackBrush = CreateSolidBrush(kTrackColor);
        FillRect(memDC, &track, trackBrush);
        DeleteObject(trackBrush);

        const int32_t fillWidth = static_cast<int32_t>((track.right - track.left) * progress_);
        if (fillWidth > 0) {
            RECT fill{ track.left, track.top, track.left + fillWidth, track.bottom };
            HBRUSH fillBrush = CreateSolidBrush(kFillColor);
            FillRect(memDC, &fill, fillBrush);
            DeleteObject(fillBrush);
        }

        // 細目（シェーダ名・テクスチャ名など）
        RECT detailRect{ margin, Scale(132, dpi_), right, Scale(156, dpi_) };
        SelectObject(memDC, detailFont_);
        SetTextColor(memDC, kDetailColor);
        DrawTextW(memDC, detail_.c_str(), -1, &detailRect,
            DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);

        SelectObject(memDC, oldFont);

        BitBlt(targetDC, 0, 0, width_, height_, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDC);
    }

    LRESULT CALLBACK SplashScreen::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        auto* self = reinterpret_cast<SplashScreen*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
        case WM_ERASEBKGND:
            // 背景は Render が全面塗るので、ここで塗ると二度手間＝ちらつきになる
            return 1;

        case WM_PAINT:
            if (self) {
                PAINTSTRUCT ps{};
                HDC dc = BeginPaint(hwnd, &ps);
                self->Render(dc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;

        case WM_CLOSE:
            // 起動シーケンスの途中で閉じられると、初期化済み／未初期化が混ざった状態で
            // 終了処理へ入ってしまう。スプラッシュは Alt+F4 では閉じない
            return 0;

        case WM_DESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}
