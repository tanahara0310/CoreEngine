#include "App/ProfilerApp.h"
#include "Editor/ImGui/EditorStyle.h"
#include "Launch/AppResource.h"
#include "Text/TextFormat.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    using Microsoft::WRL::ComPtr;
    using CoreEngine::Profiler::ProfilerApp;
    namespace Theme = CoreEngine::Editor::Theme;

    constexpr wchar_t kWindowClassName[] = L"CoreEngineProfilerWindow";

    /// @brief 窓の大きさ（拡大率 100% のとき）
    constexpr int kBaseWidth = 1440;
    constexpr int kBaseHeight = 900;

    ComPtr<ID3D11Device> g_device;
    ComPtr<ID3D11DeviceContext> g_context;
    ComPtr<IDXGISwapChain> g_swapChain;
    ComPtr<ID3D11RenderTargetView> g_renderTarget;
    UINT g_resizeWidth = 0;
    UINT g_resizeHeight = 0;
    ProfilerApp* g_app = nullptr;
    float g_initialDpiScale = 1.0f;

    /// @brief バックバッファの描画先（RTV）を sRGB の形式で作る。書いたリニアの色は sRGB へ直して入る
    void CreateRenderTarget()
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        D3D11_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        g_device->CreateRenderTargetView(backBuffer.Get(), &desc, &g_renderTarget);
    }

    bool CreateDevice(HWND hwnd)
    {
        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hwnd;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained = {};
        HRESULT hr = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &obtained, &g_context);
        if (hr == DXGI_ERROR_UNSUPPORTED) {
            hr = ::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &obtained, &g_context);
        }
        if (FAILED(hr)) {
            return false;
        }
        CreateRenderTarget();
        return true;
    }

    void CleanupDevice()
    {
        g_renderTarget.Reset();
        g_swapChain.Reset();
        g_context.Reset();
        g_device.Reset();
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
            return TRUE;
        }
        switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_resizeWidth = LOWORD(lParam);
                g_resizeHeight = HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            // Alt を押したときにメニューの操作へ入らないようにする
            if ((wParam & 0xfff0) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_CLOSE:
            if (g_app) {
                g_app->RequestQuit();
                return 0;
            }
            break;
        case WM_DROPFILES:
        {
            const HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT length = ::DragQueryFileW(drop, 0, nullptr, 0);
            if (length > 0 && g_app) {
                std::wstring path(length, L'\0');
                ::DragQueryFileW(drop, 0, path.data(), length + 1);
                g_app->RequestOpen(CoreEngine::Profiler::Text::ToUtf8(path));
            }
            ::DragFinish(drop);
            return 0;
        }
        case WM_DPICHANGED:
        {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (ImGui::GetCurrentContext()) {
                const float scale = static_cast<float>(HIWORD(wParam)) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
                ImGui::GetStyle().FontScaleDpi = scale / g_initialDpiScale;
            }
            return 0;
        }
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    /// @brief 起動の引数（exe の名前を除く）
    std::vector<std::wstring> CommandLineArguments()
    {
        std::vector<std::wstring> arguments;
        int count = 0;
        if (LPWSTR* list = ::CommandLineToArgvW(::GetCommandLineW(), &count)) {
            for (int i = 1; i < count; ++i) {
                arguments.emplace_back(list[i]);
            }
            ::LocalFree(list);
        }
        return arguments;
    }

    /// @brief 主のモニタの作業領域の真ん中に、拡大率に合わせた大きさで窓を作る
    HWND CreateMainWindow(HINSTANCE instance)
    {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_CLASSDC;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = CoreEngine::LoadAppIcon(false);
        windowClass.hIconSm = CoreEngine::LoadAppIcon(true);
        windowClass.lpszClassName = kWindowClassName;
        ::RegisterClassExW(&windowClass);

        const HMONITOR monitor = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info = { sizeof(info) };
        ::GetMonitorInfoW(monitor, &info);
        g_initialDpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);

        const RECT& work = info.rcWork;
        const int workWidth = work.right - work.left;
        const int workHeight = work.bottom - work.top;
        const int width = std::min(static_cast<int>(kBaseWidth * g_initialDpiScale), workWidth);
        const int height = std::min(static_cast<int>(kBaseHeight * g_initialDpiScale), workHeight);
        const int x = work.left + (workWidth - width) / 2;
        const int y = work.top + (workHeight - height) / 2;

        const HWND hwnd = ::CreateWindowExW(0, kWindowClassName, L"CoreEngine Profiler", WS_OVERLAPPEDWINDOW,
            x, y, width, height, nullptr, nullptr, instance, nullptr);
        if (hwnd) {
            const BOOL dark = TRUE;
            ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            ::DragAcceptFiles(hwnd, TRUE);
        }
        return hwnd;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    const HWND hwnd = CreateMainWindow(instance);
    if (!hwnd || !CreateDevice(hwnd)) {
        ::MessageBoxW(nullptr, L"画面を作れませんでした（Direct3D 11 が使えません）。", L"CoreEngine Profiler", MB_ICONERROR);
        CleanupDevice();
        if (hwnd) {
            ::DestroyWindow(hwnd);
        }
        ::UnregisterClassW(kWindowClassName, instance);
        if (SUCCEEDED(comResult)) {
            ::CoUninitialize();
        }
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    Theme::ApplyImGuiStyle();
    Theme::ApplyPlotStyle();
    Theme::LoadFonts(g_initialDpiScale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device.Get(), g_context.Get());

    {
        ProfilerApp app(CommandLineArguments());
        g_app = &app;
        std::wstring shownTitle;

        bool running = true;
        while (running) {
            MSG message;
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
                if (message.message == WM_QUIT) {
                    running = false;
                }
            }
            if (!running || app.ShouldQuit()) {
                break;
            }
            if (::IsIconic(hwnd)) {
                ::Sleep(50);
                continue;
            }

            if (g_resizeWidth != 0 && g_resizeHeight != 0) {
                g_renderTarget.Reset();
                g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_resizeWidth = g_resizeHeight = 0;
                CreateRenderTarget();
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app.Frame();
            ImGui::Render();

            const ImVec4 clear = Theme::kDeepest;
            const float clearColor[4] = { clear.x, clear.y, clear.z, 1.0f };
            ID3D11RenderTargetView* target = g_renderTarget.Get();
            g_context->OMSetRenderTargets(1, &target, nullptr);
            g_context->ClearRenderTargetView(target, clearColor);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (g_swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED) {
                ::Sleep(10);
            }

            app.AfterFrame(hwnd);
            if (app.ShouldQuit()) {
                running = false;
            }

            const std::wstring title = app.WindowTitle();
            if (title != shownTitle) {
                ::SetWindowTextW(hwnd, title.c_str());
                shownTitle = title;
            }
        }
        g_app = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDevice();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(kWindowClassName, instance);
    if (SUCCEEDED(comResult)) {
        ::CoUninitialize();
    }
    return 0;
}
