#include "pch.h"
#include "SplashScreen.h"

#include "EngineSystem/EngineVersion.h"
#include "EngineSystem/Settings/ProjectSettings.h"
#include "Utility/Path/ProjectPaths.h"
#include "WinApp/WinApp.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace CoreEngine
{
    namespace
    {
        constexpr const wchar_t* kSplashClassName = L"CoreEngineSplashClass";
        constexpr const wchar_t* kEngineName = L"CoreEngine";

        // 96 DPI 基準の論理サイズ。実 DPI に合わせて拡大する
        constexpr int32_t kBaseWidth = 640;
        constexpr int32_t kBaseHeight = 360;

        // 再描画の間引き間隔。StartupProgress::Tick はシェーダ 1 本ごとに来るので、
        // 毎回描くと GDI の分だけ起動が伸びる
        constexpr ULONGLONG kRepaintIntervalMs = 33;

        // 配置（96 DPI 基準）
        constexpr int32_t kMarginLeft = 28;
        constexpr int32_t kMarginRight = 26;
        constexpr int32_t kMarkSize = 40;
        constexpr int32_t kMarkTop = 251;
        constexpr int32_t kNameGap = 12;
        constexpr int32_t kBarHeight = 3;

        constexpr COLORREF kEdgeColor = RGB(30, 36, 44);
        constexpr COLORREF kTextShadowColor = RGB(0, 0, 0);
        constexpr COLORREF kNameColor = RGB(255, 255, 255);
        constexpr COLORREF kVersionColor = RGB(164, 170, 178);
        constexpr COLORREF kProjectColor = RGB(236, 240, 244);
        constexpr COLORREF kLabelColor = RGB(226, 230, 235);
        constexpr COLORREF kPercentColor = RGB(178, 184, 192);
        constexpr COLORREF kDetailColor = RGB(112, 118, 126);
        constexpr COLORREF kBarStartColor = RGB(111, 211, 224);
        constexpr COLORREF kBarEndColor = RGB(255, 242, 196);

        // 画像の下側（文字の帯）と上端を暗くする色
        constexpr COLORREF kShadeColor = RGB(5, 10, 16);

        // 画像が無いときの背景（上から下への縦のグラデーション）
        constexpr COLORREF kFallbackTopColor = RGB(27, 39, 56);
        constexpr COLORREF kFallbackBottomColor = RGB(11, 17, 24);

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

        /// @brief 96dpi 基準の値を実 DPI へスケールする
        int32_t Scale(int32_t value, uint32_t dpi)
        {
            return MulDiv(value, static_cast<int32_t>(dpi), 96);
        }

        /// @brief DPI に合わせた大きさ（96dpi 基準のピクセル）の UI フォントを作る
        HFONT CreateUiFont(int32_t pixelSize, int32_t weight, uint32_t dpi)
        {
            return CreateFontW(
                -Scale(pixelSize, dpi), 0, 0, 0,
                weight, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Yu Gothic UI");
        }

        /// @brief 2 色を t（0〜1）で混ぜる
        COLORREF LerpColor(COLORREF a, COLORREF b, float t)
        {
            const auto mix = [t](BYTE x, BYTE y) {
                return static_cast<BYTE>(std::lround(x + (y - x) * t));
            };
            return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
        }

        /// @brief GradientFill の頂点（色は 16bit の階調）
        TRIVERTEX MakeVertex(LONG x, LONG y, COLORREF color)
        {
            TRIVERTEX vertex{};
            vertex.x = x;
            vertex.y = y;
            vertex.Red = static_cast<COLOR16>(GetRValue(color) << 8);
            vertex.Green = static_cast<COLOR16>(GetGValue(color) << 8);
            vertex.Blue = static_cast<COLOR16>(GetBValue(color) << 8);
            return vertex;
        }

        /// @brief BGRX の 1 画素に色を重ねる
        uint32_t BlendPixel(uint32_t pixel, COLORREF color, float alpha)
        {
            const auto mix = [alpha](uint32_t x, uint32_t y) {
                return static_cast<uint32_t>(std::lround(x + (static_cast<float>(y) - static_cast<float>(x)) * alpha));
            };
            const uint32_t b = mix(pixel & 0xFFu, GetBValue(color));
            const uint32_t g = mix((pixel >> 8) & 0xFFu, GetGValue(color));
            const uint32_t r = mix((pixel >> 16) & 0xFFu, GetRValue(color));
            return b | (g << 8) | (r << 16);
        }

        /// @brief 縦位置 t（0 が上端・1 が下端）で背景を暗くする量
        /// @details 下 6 割から文字の帯へ向けて濃くし、上端も右上のプロジェクト名の分だけ少し暗くする
        float ShadeAt(float t)
        {
            if (t < 0.16f) {
                return 0.30f * (1.0f - t / 0.16f);
            }
            if (t < 0.38f) {
                return 0.0f;
            }
            if (t < 0.68f) {
                return 0.55f * (t - 0.38f) / 0.30f;
            }
            return 0.55f + 0.37f * (t - 0.68f) / 0.32f;
        }

        /// @brief 画像を読み、縦横比を保ったまま width × height を覆うように中央で切り抜いて縮める（BGRX 32bit）
        bool LoadCoverPixels(const std::filesystem::path& path, uint32_t width, uint32_t height, std::vector<uint32_t>& pixels)
        {
            using Microsoft::WRL::ComPtr;

            // WIC は COM を使う。起動シーケンスが COM を初期化するより前に呼ばれるので、ここで初期化して戻す
            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool loaded = false;
            {
                ComPtr<IWICImagingFactory> factory;
                ComPtr<IWICBitmapDecoder> decoder;
                ComPtr<IWICBitmapFrameDecode> frame;
                UINT sourceWidth = 0;
                UINT sourceHeight = 0;
                if (SUCCEEDED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
                    && SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))
                    && SUCCEEDED(decoder->GetFrame(0, &frame))
                    && SUCCEEDED(frame->GetSize(&sourceWidth, &sourceHeight))
                    && sourceWidth > 0 && sourceHeight > 0) {
                    // 窓を覆う最小の倍率で、はみ出す分を上下か左右から均等に切り落とす
                    const double scale = (std::max)(static_cast<double>(width) / sourceWidth,
                                                    static_cast<double>(height) / sourceHeight);
                    const INT cropWidth = std::clamp(static_cast<INT>(std::lround(width / scale)), 1, static_cast<INT>(sourceWidth));
                    const INT cropHeight = std::clamp(static_cast<INT>(std::lround(height / scale)), 1, static_cast<INT>(sourceHeight));
                    const WICRect crop{
                        (static_cast<INT>(sourceWidth) - cropWidth) / 2,
                        (static_cast<INT>(sourceHeight) - cropHeight) / 2,
                        cropWidth, cropHeight };

                    ComPtr<IWICFormatConverter> converter;
                    ComPtr<IWICBitmapClipper> clipper;
                    ComPtr<IWICBitmapScaler> scaler;
                    loaded = SUCCEEDED(factory->CreateFormatConverter(&converter))
                        && SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR,
                            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
                        && SUCCEEDED(factory->CreateBitmapClipper(&clipper))
                        && SUCCEEDED(clipper->Initialize(converter.Get(), &crop))
                        && SUCCEEDED(factory->CreateBitmapScaler(&scaler))
                        && SUCCEEDED(scaler->Initialize(clipper.Get(), width, height, WICBitmapInterpolationModeHighQualityCubic));
                    if (loaded) {
                        pixels.resize(static_cast<size_t>(width) * height);
                        loaded = SUCCEEDED(scaler->CopyPixels(nullptr, width * 4,
                            static_cast<UINT>(pixels.size() * sizeof(uint32_t)), reinterpret_cast<BYTE*>(pixels.data())));
                    }
                }
            }
            if (SUCCEEDED(com)) {
                ::CoUninitialize();
            }
            return loaded;
        }

        /// @brief エンジンのマーク（菱形の枠 2 重と中心の円）を白で描き込む
        /// @details 40 × 40 の升目で形を決め、1 画素を 4 × 4 に分けて覆う割合でなめらかにする
        void DrawMark(std::vector<uint32_t>& pixels, int32_t width, int32_t height, int32_t left, int32_t top, int32_t size)
        {
            constexpr int kSamples = 4;
            constexpr double kGrid = 40.0;
            constexpr double kCenter = kGrid * 0.5;
            constexpr double kSqrt2 = 1.41421356;
            const double unit = kGrid / size;

            for (int32_t y = 0; y < size; ++y) {
                const int32_t py = top + y;
                if (py < 0 || py >= height) {
                    continue;
                }
                for (int32_t x = 0; x < size; ++x) {
                    const int32_t px = left + x;
                    if (px < 0 || px >= width) {
                        continue;
                    }
                    double coverage = 0.0;
                    for (int sy = 0; sy < kSamples; ++sy) {
                        for (int sx = 0; sx < kSamples; ++sx) {
                            const double u = (x + (sx + 0.5) / kSamples) * unit - kCenter;
                            const double v = (y + (sy + 0.5) / kSamples) * unit - kCenter;
                            const double diamond = std::abs(u) + std::abs(v);
                            if (u * u + v * v <= 4.2 * 4.2) {
                                coverage += 1.0;
                            } else if (std::abs(diamond - 17.0) <= 1.2 * kSqrt2) {
                                coverage += 0.9;
                            } else if (std::abs(diamond - 10.0) <= 0.72 * kSqrt2) {
                                coverage += 0.45;
                            }
                        }
                    }
                    coverage /= kSamples * kSamples;
                    if (coverage > 0.0) {
                        uint32_t& pixel = pixels[static_cast<size_t>(py) * width + px];
                        pixel = BlendPixel(pixel, RGB(255, 255, 255), static_cast<float>(coverage));
                    }
                }
            }
        }

        /// @brief 1 画素ずらした影を付けて文字を描く（明るい画像の上でも読めるように）
        void DrawShadowedText(HDC dc, const std::wstring& text, RECT rect, UINT format, COLORREF color, int32_t offset)
        {
            RECT shadow = rect;
            OffsetRect(&shadow, offset, offset);
            SetTextColor(dc, kTextShadowColor);
            DrawTextW(dc, text.c_str(), -1, &shadow, format);
            SetTextColor(dc, color);
            DrawTextW(dc, text.c_str(), -1, &rect, format);
        }
    }

    std::filesystem::path SplashScreen::ResolveImagePath()
    {
        std::error_code ec;
        const std::string& configured = ProjectSettings::Get().GetSplashImage();
        if (!configured.empty()) {
            std::filesystem::path path = ProjectPaths::Resolve(configured);
            if (std::filesystem::is_regular_file(path, ec)) {
                return path;
            }
        }
        std::filesystem::path fallback = ProjectPaths::Resolve(kDefaultImage);
        return std::filesystem::is_regular_file(fallback, ec) ? fallback : std::filesystem::path{};
    }

    SplashScreen::~SplashScreen()
    {
        Close();
    }

    void SplashScreen::Show(HINSTANCE hInstance, const std::string& projectName)
    {
        if (hwnd_) {
            return;
        }

        hInstance_ = hInstance;
        projectName_ = Utf8ToWide(projectName);
        versionText_ = L"VERSION " + Utf8ToWide(kEngineVersion);
#ifdef CORE_EDITOR
        versionText_ += L" ・ EDITOR";
#endif

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

        // メインのモニター（メインウィンドウを出すモニター）の中央に出す（作業領域基準）
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        RECT area{ 0, 0, 1280, 720 };
        if (GetMonitorInfo(WinApp::GetPrimaryMonitor(), &monitorInfo)) {
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

        nameFont_ = CreateUiFont(26, FW_SEMIBOLD, dpi_);
        versionFont_ = CreateUiFont(12, FW_NORMAL, dpi_);
        projectFont_ = CreateUiFont(12, FW_SEMIBOLD, dpi_);
        labelFont_ = CreateUiFont(13, FW_NORMAL, dpi_);
        detailFont_ = CreateUiFont(11, FW_NORMAL, dpi_);

        BuildBackdrop();

        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        Repaint();
    }

    void SplashScreen::BuildBackdrop()
    {
        const auto width = static_cast<uint32_t>(width_);
        const auto height = static_cast<uint32_t>(height_);
        std::vector<uint32_t> pixels;

        const std::filesystem::path imagePath = ResolveImagePath();
        if (imagePath.empty() || !LoadCoverPixels(imagePath, width, height, pixels)) {
            // 画像が無い・読めないときは縦のグラデーションを敷く
            pixels.assign(static_cast<size_t>(width) * height, 0u);
            for (uint32_t y = 0; y < height; ++y) {
                const COLORREF color = LerpColor(kFallbackTopColor, kFallbackBottomColor, static_cast<float>(y) / (std::max)(1u, height - 1));
                const uint32_t pixel = GetBValue(color) | (GetGValue(color) << 8) | (GetRValue(color) << 16);
                std::fill_n(pixels.begin() + static_cast<size_t>(y) * width, width, pixel);
            }
        }

        for (uint32_t y = 0; y < height; ++y) {
            const float shade = ShadeAt(static_cast<float>(y) / (std::max)(1u, height - 1));
            if (shade <= 0.0f) {
                continue;
            }
            uint32_t* row = pixels.data() + static_cast<size_t>(y) * width;
            for (uint32_t x = 0; x < width; ++x) {
                row[x] = BlendPixel(row[x], kShadeColor, shade);
            }
        }

        DrawMark(pixels, width_, height_, Scale(kMarginLeft, dpi_), Scale(kMarkTop, dpi_), Scale(kMarkSize, dpi_));

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width_;
        info.bmiHeader.biHeight = -height_;   // 負の高さ＝上の行から並べる
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        backdrop_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (backdrop_ && bits) {
            std::memcpy(bits, pixels.data(), pixels.size() * sizeof(uint32_t));
        }
    }

    void SplashScreen::SetStatus(float progress, const std::string& label)
    {
        SetProgress(progress);
        label_ = Utf8ToWide(label);
        // ステップが変わったら細目はいったん消す（前のステップの残骸を出さない）
        detail_.clear();
    }

    void SplashScreen::SetProgress(float progress)
    {
        progress_ = (std::max)(progress_, std::clamp(progress, 0.0f, 1.0f));
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
        DestroyResources();
    }

    void SplashScreen::DestroyResources()
    {
        for (HFONT* font : { &nameFont_, &versionFont_, &projectFont_, &labelFont_, &detailFont_ }) {
            if (*font) {
                DeleteObject(*font);
                *font = nullptr;
            }
        }
        if (backdrop_) {
            DeleteObject(backdrop_);
            backdrop_ = nullptr;
        }
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

        // 組み立て済みの背景を敷く
        if (backdrop_) {
            HDC backdropDC = CreateCompatibleDC(targetDC);
            if (backdropDC) {
                HGDIOBJ oldBackdrop = SelectObject(backdropDC, backdrop_);
                BitBlt(memDC, 0, 0, width_, height_, backdropDC, 0, 0, SRCCOPY);
                SelectObject(backdropDC, oldBackdrop);
                DeleteDC(backdropDC);
            }
        }

        HBRUSH edgeBrush = CreateSolidBrush(kEdgeColor);
        FrameRect(memDC, &full, edgeBrush);
        DeleteObject(edgeBrush);

        SetBkMode(memDC, TRANSPARENT);

        const int32_t left = Scale(kMarginLeft, dpi_);
        const int32_t right = width_ - Scale(kMarginRight, dpi_);
        const int32_t shadowOffset = (std::max)(1, Scale(1, dpi_));
        constexpr UINT kLine = DT_SINGLELINE | DT_NOPREFIX;

        // 右上: プロジェクト名
        HGDIOBJ oldFont = SelectObject(memDC, projectFont_);
        DrawShadowedText(memDC, projectName_, RECT{ left, Scale(18, dpi_), right, Scale(38, dpi_) },
            DT_RIGHT | kLine | DT_END_ELLIPSIS, kProjectColor, shadowOffset);

        // 左下: マークの右にエンジンの名前と版
        const int32_t nameLeft = Scale(kMarginLeft + kMarkSize + kNameGap, dpi_);
        SelectObject(memDC, versionFont_);
        SetTextCharacterExtra(memDC, Scale(1, dpi_));
        SetTextColor(memDC, kVersionColor);
        RECT versionRect{ nameLeft, Scale(279, dpi_), right, Scale(297, dpi_) };
        DrawTextW(memDC, versionText_.c_str(), -1, &versionRect, DT_LEFT | kLine | DT_END_ELLIPSIS);
        SetTextCharacterExtra(memDC, 0);

        SelectObject(memDC, nameFont_);
        DrawShadowedText(memDC, kEngineName, RECT{ nameLeft, Scale(244, dpi_), right, Scale(280, dpi_) },
            DT_LEFT | kLine | DT_END_ELLIPSIS, kNameColor, shadowOffset);

        // 今のステップ名（左）とパーセント（右）
        SelectObject(memDC, labelFont_);
        const std::wstring percent = std::to_wstring(static_cast<int32_t>(progress_ * 100.0f + 0.5f)) + L"%";
        RECT percentRect{ left, Scale(312, dpi_), right, Scale(331, dpi_) };
        SetTextColor(memDC, kPercentColor);
        DrawTextW(memDC, percent.c_str(), -1, &percentRect, DT_RIGHT | kLine);

        RECT measured{ 0, 0, 0, 0 };
        DrawTextW(memDC, percent.c_str(), -1, &measured, DT_CALCRECT | kLine);
        const std::wstring label = (progress_ < 1.0f && !label_.empty()) ? label_ + L" …" : label_;
        RECT labelRect{ left, percentRect.top, right - (measured.right - measured.left) - Scale(12, dpi_), percentRect.bottom };
        SetTextColor(memDC, kLabelColor);
        DrawTextW(memDC, label.c_str(), -1, &labelRect, DT_LEFT | kLine | DT_END_ELLIPSIS);

        // 細目（シェーダ名・テクスチャ名など）
        SelectObject(memDC, detailFont_);
        SetTextColor(memDC, kDetailColor);
        RECT detailRect{ left, Scale(333, dpi_), right, Scale(350, dpi_) };
        DrawTextW(memDC, detail_.c_str(), -1, &detailRect, DT_LEFT | kLine | DT_PATH_ELLIPSIS);

        SelectObject(memDC, oldFont);

        // 下端の進捗の線（左の色から、進んだ割合に応じて右の色へ寄せる）
        const int32_t fillWidth = static_cast<int32_t>(std::lround(width_ * progress_));
        if (fillWidth > 0) {
            const COLORREF endColor = LerpColor(kBarStartColor, kBarEndColor, progress_);
            TRIVERTEX vertices[2]{
                MakeVertex(0, height_ - Scale(kBarHeight, dpi_), kBarStartColor),
                MakeVertex(fillWidth, height_, endColor),
            };
            GRADIENT_RECT gradient{ 0, 1 };
            GradientFill(memDC, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
        }

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
