#include "pch.h"
#include "Editor/Launcher/ProjectLauncher.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiManager.h"
#include "Editor/Launcher/ProjectBrowser.h"
#include "Editor/Launcher/ProjectList.h"
#include "Editor/Launcher/ProjectThumbnails.h"
#include "EngineSystem/EngineConfig.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/GraphicsCoreDesc.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"
#include "Utility/Path/ProjectPaths.h"
#include "WinApp/WinApp.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace CoreEngine::Editor
{
    namespace
    {
        namespace Theme = Editor::Theme;

        /// 画面を出させる起動の引数
        constexpr std::wstring_view kLauncherOption = L"--launcher";

        /// @brief 起動の引数に `--launcher` があるか
        bool HasLauncherOption()
        {
            int count = 0;
            LPWSTR* const args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
            if (!args) {
                return false;
            }
            bool found = false;
            for (int i = 1; i < count; ++i) {
                if (std::wstring_view(args[i]) == kLauncherOption) {
                    found = true;
                    break;
                }
            }
            ::LocalFree(args);
            return found;
        }

        /// @brief path を表示用の UTF-8 にする（区切りは Windows の '\'）
        std::string ToDisplay(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief 窓いっぱいの背景の真ん中に、プロジェクトの画面を描く
        void DrawCentered(ProjectBrowser& browser)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::kDeepest);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("##Launcher", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            // 中央に、フォントの大きさを基準にした枠を置く
            const float fontSize = ImGui::GetFontSize();
            const ImVec2 area = ImGui::GetContentRegionAvail();
            const ImVec2 size(std::min(area.x - fontSize * 2.0f, fontSize * 92.0f),
                              std::min(area.y - fontSize * 2.0f, fontSize * 56.0f));
            ImGui::SetCursorPos(ImVec2((area.x - size.x) * 0.5f, (area.y - size.y) * 0.5f));

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kWindow);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::BeginChild("##Panel", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            ImGui::PopStyleVar();
            browser.Draw();
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            ImGui::End();
        }
    }

    bool ProjectLauncher::Run(WinApp& winApp, const EngineConfig& config)
    {
        ProjectList list;
        list.Load();

        // 前回のプロジェクトを自動で開く
        if (!HasLauncherOption() && list.GetOpenLastOnStartup()) {
            const std::filesystem::path last = list.LastOpened();
            if (!last.empty() && ProjectPaths::OpenProject(last)) {
                return true;
            }
        }

        // エクスプローラーで表示（ShellExecute）が COM を使う。エンジンは後で別の方式で初期化するので、戻る前に閉じる
        const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        const HWND hwnd = winApp.GetHwnd();
        GraphicsCore graphics;
        GraphicsCoreDesc desc{};
        desc.hwnd = hwnd;
        desc.clientWidth = winApp.GetClientWidth();
        desc.clientHeight = winApp.GetClientHeight();
        desc.enableDebugLayer = config.enableDebugLayer;
        desc.enableGPUBasedValidation = config.enableGPUBasedValidation;
        desc.framesInFlight = 2;
        desc.maxSRVDescriptors = 256;
        desc.maxRTVDescriptors = 16;
        desc.maxDSVDescriptors = 1;
        graphics.Initialize(desc);
        winApp.SetResizeCallback([&graphics](int32_t width, int32_t height) {
            graphics.OnWindowResize(width, height);
            });

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGuiManager::ApplyCustomTheme();
        ImGuiManager::LoadFonts(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));
        ImGui_ImplWin32_Init(hwnd);

        SwapChain& swapChain = graphics.GetSwapChain();
        DescriptorHandle fontDescriptor = graphics.GetDescriptorAllocator()->AllocateSRVHandle("LauncherFont");
        ImGui_ImplDX12_Init(graphics.GetDevice(), static_cast<int>(swapChain.BufferCount()), swapChain.RTVFormat(),
            graphics.GetSRVHeap(), fontDescriptor.cpuHandle, fontDescriptor.gpuHandle);
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);
        ImGui_ImplDX12_CreateDeviceObjects();

        ::SetWindowTextW(hwnd, L"CoreEngine — プロジェクトを開く");
        ::ShowWindow(hwnd, SW_SHOW);
        ::SetForegroundWindow(hwnd);

        std::optional<ProjectThumbnails> thumbnails;
        thumbnails.emplace(graphics);
        ProjectBrowser browser(list, hwnd);
        browser.SetThumbnails(&*thumbnails);
        bool closed = false;
        while (true) {
            if (winApp.ProcessMessage()) {
                closed = true;
                break;
            }
            if (::IsIconic(hwnd)) {
                ::Sleep(16);
                continue;
            }

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            DrawCentered(browser);
            ImGui::Render();

            // バックバッファを描画先にして、塗りつぶしてから ImGui を描き、表示へ戻す
            const FrameContext frame = graphics.BeginFrame();
            const uint32_t backBufferIndex = swapChain.CurrentBackBufferIndex();
            Barrier::Transition(frame.cmdList, swapChain.BackBuffer(backBufferIndex), D3D12_RESOURCE_STATE_RENDER_TARGET);
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = swapChain.RTV(backBufferIndex);
            frame.cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const float clearColor[4] = { Theme::kDeepest.x, Theme::kDeepest.y, Theme::kDeepest.z, 1.0f };
            frame.cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), frame.cmdList);
            Barrier::Transition(frame.cmdList, swapChain.BackBuffer(backBufferIndex), D3D12_RESOURCE_STATE_PRESENT);
            graphics.EndFrame(1);

            // 選ばれたプロジェクトを開く
            const std::filesystem::path chosen = browser.TakeChosen();
            if (chosen.empty()) {
                continue;
            }
            if (ProjectPaths::OpenProject(chosen)) {
                break;
            }
            browser.SetStatus(ToDisplay(chosen.filename()) + " はプロジェクトのフォルダではありません", true);
        }

        graphics.WaitForGpuIdle();
        browser.SetThumbnails(nullptr);
        thumbnails.reset();
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        winApp.SetResizeCallback(nullptr);
        graphics.Shutdown();

        // エンジンの初期化が済むまで窓を隠す（起動の間はスプラッシュが出る）
        ::ShowWindow(hwnd, SW_HIDE);
        ::SetWindowTextW(hwnd, config.GetWindowTitleWide().c_str());

        if (SUCCEEDED(comResult)) {
            ::CoUninitialize();
        }
        return !closed;
    }
}

#endif // CORE_EDITOR
