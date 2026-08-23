#include "pch.h"
#include "GameOutputWindow.h"

#include <algorithm>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/FullScreen.h"
#include "Utility/Logger/Logger.h"
#include "WinApp/WinApp.h"

namespace CoreEngine
{
    namespace {
        // エンジン本体と同じ構成にする。バッファは非 sRGB、sRGB 変換は RTV 側で掛ける
        //（Flip モデルのスワップチェーンは _SRGB フォーマットを受け付けない）。
        constexpr DXGI_FORMAT kBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        constexpr DXGI_FORMAT kRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }

    GameOutputWindow::~GameOutputWindow(){
        Finalize();
    }

    void GameOutputWindow::Initialize(GraphicsCore* dxCommon, PostEffectManager* postEffectManager, HWND mainHwnd){
        dxCommon_ = dxCommon;
        postEffectManager_ = postEffectManager;
        mainHwnd_ = mainHwnd;
    }

    void GameOutputWindow::Finalize(){
        Close();

        if (windowClassRegistered_) {
            ::UnregisterClassW(kWindowClassName, ::GetModuleHandleW(nullptr));
            windowClassRegistered_ = false;
        }

        dxCommon_ = nullptr;
        postEffectManager_ = nullptr;
    }

    bool GameOutputWindow::ConsumeCloseRequest(){
        const bool requested = closeRequested_;
        closeRequested_ = false;
        return requested;
    }

    LRESULT CALLBACK GameOutputWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
        auto* self = reinterpret_cast<GameOutputWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (msg) {
        case WM_NCCREATE: {
            // CreateWindowEx へ渡した this をウィンドウへ結び付ける
            const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            break;
        }

        case WM_CLOSE:
            // ここで DestroyWindow はしない。GPU がまだスワップチェーンを使っている可能性があるため、
            // 破棄はフレームの安全な位置（ApplyPendingRequests）で行う。
            if (self) {
                self->closeRequested_ = true;
                self->visibleRequested_ = false;
            }
            return 0;

        case WM_SIZE:
            if (self && wparam != SIZE_MINIMIZED) {
                // lparam の LOWORD/HIWORD は使わないこと。
                // 拡大率の違うモニタへ移した直後は、ウィンドウがまだ元のモニタの DPI 文脈に
                // いるため、lparam が移動前のスケールの値（= 実ピクセルの 0.8 倍など）で届く。
                // GetClientRect は常に実ピクセルを返す（本体の WinApp::WindowProc と同じ方式）。
                RECT clientRect{};
                ::GetClientRect(hwnd, &clientRect);
                const int32_t width = clientRect.right - clientRect.left;
                const int32_t height = clientRect.bottom - clientRect.top;
                if (width > 0 && height > 0
                    && (width != self->clientWidth_ || height != self->clientHeight_)) {
                    // 実際のリサイズもコマンド記録の外で行う
                    self->resizeRequested_ = true;
                    self->requestedWidth_ = width;
                    self->requestedHeight_ = height;
                }
            }
            return 0;

        case WM_DPICHANGED:
            // Per-Monitor Aware V2 では、別スケールのモニタへ移った時に OS が提示する矩形へ
            // 追従するのがアプリの責務。従わないとウィンドウ枠と中身の縮尺がずれる。
            if (const RECT* suggested = reinterpret_cast<const RECT*>(lparam)) {
                ::SetWindowPos(hwnd, nullptr,
                    suggested->left, suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_KEYDOWN:
            // このウィンドウにフォーカスがある間、エンジンの DirectInput は入力を拾わない
            //（キーボードは DISCL_FOREGROUND で本体ウィンドウに結び付いているため）。
            // フルスクリーン中でも Esc で終了できるよう、ここでも受ける。
            if (self && wparam == VK_ESCAPE && self->mainHwnd_) {
                ::PostMessageW(self->mainHwnd_, WM_CLOSE, 0, 0);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            // 背景消去はスワップチェーンが担うので、OS 側の塗り潰しは抑止する（ちらつき防止）
            return 1;

        default:
            break;
        }

        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    bool GameOutputWindow::Open(){
        if (hwnd_) {
            return true;
        }
        if (!dxCommon_) {
            return false;
        }

        if (!windowClassRegistered_) {
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(WNDCLASSEXW);
            windowClass.lpfnWndProc = WindowProc;
            windowClass.lpszClassName = kWindowClassName;
            windowClass.hInstance = ::GetModuleHandleW(nullptr);
            windowClass.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));

            if (::RegisterClassExW(&windowClass) == 0) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                    "GameOutputWindow: ウィンドウクラスの登録に失敗しました\n");
                return false;
            }
            windowClassRegistered_ = true;
        }

        // エンジン本体があるモニタ全体をボーダーレスで覆う。
        // タスクバーが隠れるのは「フォアグラウンドのウィンドウがモニタ矩形と完全一致し、
        // かつ枠を持たない」ことを OS が検出したときなので、この3条件を満たす必要がある。
        HMONITOR monitor = ::MonitorFromWindow(
            mainHwnd_ ? mainHwnd_ : ::GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        if (!::GetMonitorInfoW(monitor, &monitorInfo)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "GameOutputWindow: モニタ情報の取得に失敗しました\n");
            return false;
        }

        const RECT& monitorRect = monitorInfo.rcMonitor;
        clientWidth_ = monitorRect.right - monitorRect.left;
        clientHeight_ = monitorRect.bottom - monitorRect.top;

        // WS_POPUP はクライアント領域＝ウィンドウ領域なので AdjustWindowRect は不要
        constexpr DWORD kStyle = WS_POPUP;

        hwnd_ = ::CreateWindowExW(
            0,
            kWindowClassName,
            L"CoreEngine - Game",
            kStyle,
            monitorRect.left, monitorRect.top,
            clientWidth_, clientHeight_,
            nullptr, nullptr,
            ::GetModuleHandleW(nullptr),
            this);

        if (!hwnd_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "GameOutputWindow: ウィンドウの生成に失敗しました\n");
            return false;
        }

        if (!CreateSwapChain()) {
            Close();
            return false;
        }

        // フルスクリーン表示ではフォアグラウンドである必要がある
        //（そうでないとタスクバーがこのウィンドウの上に出たままになる）
        ::ShowWindow(hwnd_, SW_SHOW);
        ::SetForegroundWindow(hwnd_);
        ::SetFocus(hwnd_);

        Logger::GetInstance().Infof(LogCategory::Graphics, LogSubCategory::SwapChain,
            "GameOutputWindow: 生成完了 フルスクリーン ({}x{})\n", clientWidth_, clientHeight_);
        return true;
    }

    void GameOutputWindow::Close(){
        if (!hwnd_ && !swapChain_.IsValid()) {
            return;
        }

        // スワップチェーンのリソースを手放す前に、GPU がそれらを使い終わっているのを保証する
        if (dxCommon_) {
            dxCommon_->WaitForGpuIdle();
        }

        // RTV は DescriptorAllocator へ返る（開閉を繰り返してもスロットは漏れない）
        swapChain_.Shutdown();

        if (hwnd_) {
            ::DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }

        recordedThisFrame_ = false;
        resizeRequested_ = false;
    }

    bool GameOutputWindow::CreateSwapChain(){
        if (!dxCommon_ || !hwnd_) {
            return false;
        }

        SwapChainDesc desc{};
        desc.hwnd = hwnd_;
        desc.width = static_cast<uint32_t>(clientWidth_);
        desc.height = static_cast<uint32_t>(clientHeight_);
        desc.bufferCount = kBufferCount;
        desc.bufferFormat = kBufferFormat;
        desc.rtvFormat = kRtvFormat;
        desc.debugName = "GameOutputWindow";

        // エンジン本体と同じコマンドキューへ載せる。別キューにすると
        // 本体の描画完了とこのウィンドウの Present の順序を自前で同期する必要が出る。
        // RTV は本体と同じ DescriptorAllocator から確保する（Phase 3 で予約スロットが
        // 無くなったので、旧実装のような専用ミニヒープは要らない）。
        return swapChain_.Initialize(
            dxCommon_->GetDXGIFactory(),
            dxCommon_->GetCommandQueue(),
            dxCommon_->GetDescriptorAllocator(),
            desc);
    }

    void GameOutputWindow::ResizeIfRequested(){
        if (!resizeRequested_ || !swapChain_.IsValid()) {
            resizeRequested_ = false;
            return;
        }

        const int32_t width = requestedWidth_;
        const int32_t height = requestedHeight_;
        resizeRequested_ = false;

        if (width <= 0 || height <= 0) {
            return;
        }

        // ResizeBuffers はバックバッファへの参照が残っていると失敗する。
        // 本体のリサイズ経路（GraphicsCore::OnWindowResize）と同じ作法で GPU を待ってから行う。
        if (dxCommon_) {
            dxCommon_->WaitForGpuIdle();
        }

        if (!swapChain_.Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::SwapChain,
                "GameOutputWindow: リサイズに失敗しました {}x{}\n", width, height);
            return;
        }

        clientWidth_ = width;
        clientHeight_ = height;
    }

    void GameOutputWindow::ApplyPendingRequests(){
        recordedThisFrame_ = false;

        if (visibleRequested_ && !hwnd_) {
            Open();
        } else if (!visibleRequested_ && hwnd_) {
            Close();
        }

        if (hwnd_) {
            ResizeIfRequested();
        }
    }

    void GameOutputWindow::RecordDrawCommands(){
        if (!hwnd_ || !swapChain_.IsValid() || !dxCommon_ || !postEffectManager_) {
            return;
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE sourceSrv = postEffectManager_->GetFinalDisplayTextureHandle();
        if (sourceSrv.ptr == 0) {
            return;
        }

        ID3D12GraphicsCommandList* cmdList = dxCommon_->GetCommandList();
        if (!cmdList) {
            return;
        }

        const uint32_t backBufferIndex = swapChain_.CurrentBackBufferIndex();
        GpuResource& backBuffer = swapChain_.BackBuffer(backBufferIndex);
        if (!backBuffer) {
            return;
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain_.RTV(backBufferIndex);

        // PRESENT → RENDER_TARGET
        Barrier::Transition(cmdList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);

        cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        // 描画解像度はエンジン本体のクライアント領域基準。引き伸ばさず、
        // アスペクト比を保った領域だけへ転写し、余白は黒帯として残す。
        constexpr float kClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmdList->ClearRenderTargetView(rtv, kClearColor, 0, nullptr);

        const float sourceWidth = static_cast<float>(WinApp::GetCurrentClientWidthStatic());
        const float sourceHeight = static_cast<float>(WinApp::GetCurrentClientHeightStatic());
        const float targetWidth = static_cast<float>(clientWidth_);
        const float targetHeight = static_cast<float>(clientHeight_);
        if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || targetWidth <= 0.0f || targetHeight <= 0.0f) {
            return;
        }

        const float sourceAspect = sourceWidth / sourceHeight;
        float drawWidth = targetWidth;
        float drawHeight = drawWidth / sourceAspect;
        if (drawHeight > targetHeight) {
            drawHeight = targetHeight;
            drawWidth = drawHeight * sourceAspect;
        }

        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = (targetWidth - drawWidth) * 0.5f;
        viewport.TopLeftY = (targetHeight - drawHeight) * 0.5f;
        viewport.Width = drawWidth;
        viewport.Height = drawHeight;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        cmdList->RSSetViewports(1, &viewport);

        D3D12_RECT scissor{};
        scissor.left = static_cast<LONG>(viewport.TopLeftX);
        scissor.top = static_cast<LONG>(viewport.TopLeftY);
        scissor.right = static_cast<LONG>(viewport.TopLeftX + drawWidth);
        scissor.bottom = static_cast<LONG>(viewport.TopLeftY + drawHeight);
        cmdList->RSSetScissorRects(1, &scissor);

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）

        // バックバッファ用 PSO（RTV フォーマットが _SRGB）でフルスクリーン三角形を描く。
        // 本体の BackBufferPass と同じ経路なので、色の扱いも完全に一致する。
        if (auto* fullScreen = postEffectManager_->GetEffect<FullScreen>(PostEffectNames::FullScreen)) {
            fullScreen->DrawToBackBuffer(sourceSrv);
        }

        // RENDER_TARGET → PRESENT
        Barrier::Transition(cmdList, backBuffer, D3D12_RESOURCE_STATE_PRESENT);

        recordedThisFrame_ = true;
    }

    void GameOutputWindow::Present()
    {
        if (!recordedThisFrame_ || !swapChain_.IsValid()) {
            return;
        }

        // 本体は VSync 待ちで Present 済み。ここで待つとフレームが二重に律速されるため待たない。
        swapChain_.Present(0, 0);
        recordedThisFrame_ = false;
    }
}
