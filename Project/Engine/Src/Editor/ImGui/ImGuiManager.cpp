#include "pch.h"
#include "UI/UIPointer.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "ImGuiManager.h"
#include "Editor/ImGui/EditorStyle.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/Render.h"
#include "Editor/Camera/EditorCameraInput.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Editor/ImGui/GameDebugUI.h"
#include "Editor/ImGui/GameViewportOverlay.h"
#include "Input/TextInput.h"
#include "Launch/AppResource.h"
#include "WinApp/WinApp.h"
#include <ImGuizmo.h>
#include <implot.h>
#include <format>
#include <string>

namespace CoreEngine
{

    namespace
    {
        void (*originalCreatePlatformWindow)(ImGuiViewport*) = nullptr;

        /// ImGui の別のウィンドウのプロシージャ（どの別ウィンドウも同じ）
        WNDPROC originalPlatformWindowProc = nullptr;

        /// @brief 別のウィンドウへ出した Game ビューでも、ゲームの入力欄へ文字を渡してから ImGui へ回す
        LRESULT CALLBACK PlatformWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
        {
            LRESULT textResult = 0;
            if (TextInput::Get().HandleWindowMessage(hwnd, msg, wparam, lparam, textResult)) {
                return textResult;
            }
            return ::CallWindowProcW(originalPlatformWindowProc, hwnd, msg, wparam, lparam);
        }

        /// @brief ImGui が別のウィンドウ（OS のウィンドウ）を作ったあと、エンジンのアイコンを付け、文字の受け口を挟む
        void CreatePlatformWindowWithIcon(ImGuiViewport* viewport)
        {
            originalCreatePlatformWindow(viewport);
            if (const HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw)) {
                ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadAppIcon(false)));
                ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadAppIcon(true)));

                const auto previous = reinterpret_cast<WNDPROC>(
                    ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&PlatformWindowProc)));
                if (previous && previous != &PlatformWindowProc) {
                    originalPlatformWindowProc = previous;
                }
            }
        }
    }

#ifdef CORE_EDITOR
    namespace
    {
        /// @brief 暗い下地の上に小さな文字を描く
        /// @param min 下地の左上
        void DrawBadge(ImDrawList* drawList, const ImVec2& min, const std::string& text)
        {
            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const ImVec2 max(min.x + textSize.x + 12.0f, min.y + textSize.y + 4.0f);
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(Editor::Theme::WithAlpha(Editor::Theme::kDeepest, 0.8f)), 3.0f);
            drawList->AddText(ImVec2(min.x + 6.0f, min.y + 2.0f), ImGui::GetColorU32(Editor::Theme::kText), text.c_str());
        }

        /// @brief プレハブモードの間、映像の外周の青い枠と上辺の札を描く
        void DrawPrefabModeOverlay(const ImVec2& min, const ImVec2& max, const EditorStatus& status)
        {
            if (status.prefabMode.empty()) {
                return;
            }

            namespace Theme = Editor::Theme;
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImU32 blue = ImGui::GetColorU32(Theme::kAccent);
            drawList->AddRect(min, max, blue, 0.0f, 0, 3.0f);

            // 上辺の中央の札
            const std::string label = "PREFAB  " + status.prefabMode;
            const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
            const float centerX = (min.x + max.x) * 0.5f;
            const ImVec2 tabMin(centerX - labelSize.x * 0.5f - 12.0f, min.y);
            const ImVec2 tabMax(centerX + labelSize.x * 0.5f + 12.0f, min.y + labelSize.y + 2.0f);
            drawList->AddRectFilled(tabMin, tabMax, blue, 4.0f, ImDrawFlags_RoundCornersBottom);
            drawList->AddText(ImVec2(tabMin.x + 12.0f, tabMin.y + 1.0f), ImGui::GetColorU32(Theme::kText), label.c_str());

            // 左下：戻り方
            const float badgeHeight = ImGui::GetTextLineHeight() + 4.0f;
            DrawBadge(drawList, ImVec2(min.x + 8.0f, max.y - badgeHeight - 8.0f),
                "このプレハブだけを表示中（Hierarchy の「◀ シーンへ戻る」で戻る）");
        }

        /// @brief 再生モードの間、Game ビューの外周の橙枠と上辺の札・経過時間・スクリプトの実行時間を描く
        /// @param min 映像の左上
        /// @param max 映像の右下
        void DrawPlayModeOverlay(const ImVec2& min, const ImVec2& max, const EditorStatus& status)
        {
            if (status.playback == PlaybackState::Editing) {
                DrawPrefabModeOverlay(min, max, status);
                return;
            }

            namespace Theme = Editor::Theme;
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            const ImU32 warm = ImGui::GetColorU32(Theme::kWarm);
            drawList->AddRect(min, max, warm, 0.0f, 0, 2.0f);

            // 上辺の中央の札
            const char* const label = (status.playback == PlaybackState::Paused) ? "PAUSED" : "PLAYING";
            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            const float centerX = (min.x + max.x) * 0.5f;
            const ImVec2 tabMin(centerX - labelSize.x * 0.5f - 12.0f, min.y);
            const ImVec2 tabMax(centerX + labelSize.x * 0.5f + 12.0f, min.y + labelSize.y + 2.0f);
            drawList->AddRectFilled(tabMin, tabMax, warm, 4.0f, ImDrawFlags_RoundCornersBottom);
            drawList->AddText(ImVec2(tabMin.x + 12.0f, tabMin.y + 1.0f), ImGui::GetColorU32(Theme::kOnWarm), label);

            // 右上：再生を始めてから進んだゲームの時間
            const int tenths = static_cast<int>(status.playTime * 10.0f);
            const std::string elapsed = std::format("経過 {:02}:{:02}.{}", tenths / 600, (tenths / 10) % 60, tenths % 10);
            const float elapsedWidth = ImGui::CalcTextSize(elapsed.c_str()).x + 12.0f;
            DrawBadge(drawList, ImVec2(max.x - elapsedWidth - 8.0f, min.y + 8.0f), elapsed);

            // 左下：スクリプトの実行
            const std::string scripts = std::format("Script {:.2f}ms / {} 実体", status.scriptUpdateMs, status.scriptComponents);
            const float badgeHeight = ImGui::GetTextLineHeight() + 4.0f;
            DrawBadge(drawList, ImVec2(min.x + 8.0f, max.y - badgeHeight - 8.0f), scripts);
        }
    }
#endif

    void ImGuiManager::Initialize(HWND hwnd, GraphicsCore* dxCommon)
    {

        // クラス情報をメンバ変数に代入
        hwnd_ = hwnd;
        dxCommon_ = dxCommon;

        // ImGui バックエンドはバックバッファ枚数と RTV フォーマットを要求する
        const SwapChain& swapChain = dxCommon_->GetSwapChain();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // キーボードナビゲーションを有効化
        // ドッキング
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // ドッキングを有効化

        // マルチビューポート: ImGui ウィンドウをエンジンウィンドウの外（別モニタ等）へ出せるようにする。
        // 【重要】このフラグは ImGui_ImplDX12_Init / ImGui_ImplWin32_Init より前に立てること
        //         （バックエンドが Init 時にフラグを見るので、後から立てても副ウィンドウが出ない）。
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // DPI 追従（io.ConfigDpiScaleViewports）は有効にしない。
        // 拡大率の違うモニタ間では ImGui のウィンドウだけが DPI 比で縮み、中身が小さく描かれる。
        // 無効なら OS ウィンドウが少し大きくなる代わりに内容は原寸で読める。
        // なお旧 ImGuiConfigFlags_DpiEnableScaleViewports は 1.92 で無効化されており立てても効かない。

        io.ConfigWindowsMoveFromTitleBarOnly = false; // ウィンドウ全体からドラッグ移動を可能にする

        // ウィンドウの位置とドックの配置はエディタ設定の保存（EditorLayout）が持つので、ファイルへは書かない
        io.IniFilename = nullptr;

        ImGui::StyleColorsDark();
        ApplyCustomTheme();

        ImPlot::CreateContext();
        Editor::Theme::ApplyPlotStyle();

        // 副ウィンドウは OS ウィンドウとして描かれるため、角丸と半透明背景は見た目が崩れる
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // DPIスケールを取得してフォントサイズに反映
        LoadFonts(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));

        ImGui_ImplWin32_Init(hwnd_);

        // 別モニタへ出したウィンドウにもエンジンのアイコンを付ける
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        if (platformIO.Platform_CreateWindow && platformIO.Platform_CreateWindow != &CreatePlatformWindowWithIcon) {
            originalCreatePlatformWindow = platformIO.Platform_CreateWindow;
            platformIO.Platform_CreateWindow = &CreatePlatformWindowWithIcon;
        }

        // ImGui のフォント用スロットは DescriptorAllocator から正規に確保する
        // （ヒープ先頭を決め打ちで使うと他の SRV と衝突する）
        fontDescriptor_ = dxCommon_->GetDescriptorAllocator()->AllocateSRVHandle("ImGuiFont");
        ImGui_ImplDX12_Init(
            dxCommon_->GetDevice(),
            static_cast<int>(swapChain.BufferCount()),
            swapChain.RTVFormat(),
            dxCommon_->GetSRVHeap(),
            fontDescriptor_.cpuHandle,
            fontDescriptor_.gpuHandle);

        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);
        ImGui_ImplDX12_CreateDeviceObjects(); // これがないとアクセス違反が起きる

#ifdef CORE_EDITOR
        // ProjectViewの初期化
        projectView_->Initialize(dxCommon_);
#endif
    }

    void ImGuiManager::LoadFonts(float dpiScale)
    {
        if (Editor::Theme::LoadFonts(dpiScale)) {
            ImGui::GetIO().Fonts->Build();
        } else {
            OutputDebugStringA("フォントファイルが存在しません: YuGothB.ttc\n");
        }
    }

    void ImGuiManager::Begin([[maybe_unused]] PostEffectManager* postEffectManager, [[maybe_unused]] GameDebugUI* gameDebugUI)
    {

        // フレームの開始
        StartNewFrame();

        // F11 でエディタUIを丸ごと退避し、Game だけを全面表示する。
        // テキスト入力中は名前の編集などを妨げないよう無視する。
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
            ToggleEditorUi();
        }

        // 非表示中はドックスペースも作らない。Game は DrawGameViewport が
        // ドックとは無関係の全面ウィンドウとして描く。
        if (!editorUiVisible_) {
            return;
        }

        // ドッキングUIの開始（メニューバーの高さを考慮してドッキングスペースを配置）
        // レイアウト構築は BeginDockSpaceHost が DockSpace 提出前に内部で行う
        auto dockHost = dockingUI_->BeginDockSpaceHost();

        // Game ビューポートは PostEffectPass 完了後に別経路で描画する。
#ifdef CORE_EDITOR
        // Canvas ウィンドウ（UI の配置編集）。
        // 背景にはゲームの描画結果そのものを敷き、その上へ選択枠とギズモだけを重ねる
        // （Unity の Scene ビューと同じ考え方）。渡るのは 1 フレーム前の結果だが、
        // 編集操作では体感できない
        unsigned long long canvasBackground = 0;
        if (postEffectManager) {
            canvasBackground = postEffectManager->GetFinalDisplayTextureHandle().ptr;
        }
        // 選択は Hierarchy / Inspector と共有する
        canvasViewport_->DrawCanvasViewport(canvasBackground,
            gameDebugUI ? gameDebugUI->GetSceneDebugEditor() : nullptr);


        // プロジェクトビューの更新

        projectView_->Update();
#endif
    }

    void ImGuiManager::DrawGameViewport([[maybe_unused]] GraphicsCore* dxCommon, [[maybe_unused]] PostEffectManager* postEffectManager, [[maybe_unused]] GameDebugUI* gameDebugUI)
    {
#ifdef CORE_EDITOR
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle{};
        if (postEffectManager) {
            textureHandle = postEffectManager->GetFinalDisplayTextureHandle();
        }

        // 「ゲーム画面のみのウィンドウ」は専用の Win32 ウィンドウ（GameOutputWindow）が担当する。
        // ImGui のビューポートは混在 DPI 環境でサイズがずれるため、映像確認用途には使わない。

        // エディタ UI 退避中は、ドッキング状態を共有しない別ウィンドウとして全面描画する。
        // 同じ "Game" ウィンドウを使い回すとドックノードの解決先が無くなり表示が壊れる。
        if (!editorUiVisible_) {
            DrawFullscreenGameViewport(textureHandle);
            return;
        }

        if (!ImGui::Begin("Game", nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground)) {
            ImGui::End();
            return;
        }

        ImVec2 contentRegionSize = ImGui::GetContentRegionAvail();
        // クライアント領域ではなく基準解像度のアスペクト比で表示する。
        // 描画ターゲットはクライアント領域サイズだが、カメラ・UI は基準解像度の
        // 縦横比で描いているので、その比率へ戻して表示しないと絵が伸びる
        const float aspect = WinApp::GetReferenceAspect();

        float drawW = contentRegionSize.x;
        float drawH = drawW / aspect;
        if (drawH > contentRegionSize.y) {
            drawH = contentRegionSize.y;
            drawW = drawH * aspect;
        }

        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        float offsetX = (contentRegionSize.x - drawW) * 0.5f;
        float offsetY = (contentRegionSize.y - drawH) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(contentPos.x + offsetX, contentPos.y + offsetY));
        ImGui::Image((ImTextureID)textureHandle.ptr, ImVec2(drawW, drawH));

        // デバッグ窓が中間結果や印を重ねる（クリックを使ったら、ギズモと物の選択へ渡さない）
        const bool overlayUsedClick = Editor::GameViewportOverlay::DrawAll(
            { ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::IsItemHovered() });

        // 再生モードの間は、編集が停止で戻ることを映像の外周で知らせる
        DrawPlayModeOverlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), dockingUI_->GetStatus());

        // Gameビュー上のギズモ・オブジェクト選択・モデルドロップ。
        // エディタ機能の有効条件は CORE_EDITOR（Development ビルドにも必要）。_DEBUG で囲まないこと
        SceneDebugEditor* sceneDebugEditor = gameDebugUI ? gameDebugUI->GetSceneDebugEditor() : nullptr;

        if (sceneDebugEditor) {
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            const ImVec2 imageMax = ImGui::GetItemRectMax();
            const ImVec2 imageSize(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
            sceneDebugEditor->AcceptGameViewportModelDrop(imageMin, imageSize);
            const bool isImageHovered = ImGui::IsItemHovered();
            // UI の当たり判定とカメラ操作へ、ゲーム画像が画面上のどこにあるかを教える
            UIPointer::Get().SetViewRect({ imageMin.x, imageMin.y },
                                         { imageSize.x, imageSize.y }, isImageHovered);
            EditorCameraInput::SetViewportRect({ imageMin.x, imageMin.y }, { imageSize.x, imageSize.y });
            sceneDebugEditor->UpdateGameViewportInteraction(
                imageMin,
                imageSize,
                isImageHovered && !overlayUsedClick);
        }

        ImGui::End();
#endif
    }

    void ImGuiManager::DrawFullscreenGameViewport([[maybe_unused]] D3D12_GPU_DESCRIPTOR_HANDLE textureHandle)
    {
#ifdef CORE_EDITOR
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

        const bool opened = ImGui::Begin("##GameFullscreen", nullptr, flags);

        ImGui::PopStyleVar(3);

        if (opened) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            // 通常の Game ビューと同じく、基準解像度のアスペクト比で表示する
            const float aspect = WinApp::GetReferenceAspect();

            // アスペクト比を保ったまま最大化（余白は黒帯）
            float drawW = available.x;
            float drawH = drawW / aspect;
            if (drawH > available.y) {
                drawH = available.y;
                drawW = drawH * aspect;
            }

            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(
                origin.x + (available.x - drawW) * 0.5f,
                origin.y + (available.y - drawH) * 0.5f));
            ImGui::Image((ImTextureID)textureHandle.ptr, ImVec2(drawW, drawH));
            Editor::GameViewportOverlay::DrawAll(
                { ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::IsItemHovered() });

            // 戻し方が分からなくならないよう、復帰キーだけ隅に出しておく
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + 6.0f));
            ImGui::TextDisabled("F11: エディタUIを表示");
        }

        ImGui::End();
        ImGui::PopStyleColor();
#endif
    }

    void ImGuiManager::End()
    {
    }

    void ImGuiManager::Draw()
    {
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon_->GetCommandList());
    }

    void ImGuiManager::RenderPlatformWindows()
    {
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0) {
            return;
        }

        // メインビューポートの Present 後に、メインウィンドウ外へ出されたウィンドウを描画・Present する。
        // 副ウィンドウは imgui が用意した専用のコマンドキューで走るため、
        // エンジン側のコマンドリスト記録中に呼ぶと記録の入れ子になり壊れる。
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    void ImGuiManager::Finalize()
    {
#ifdef CORE_EDITOR
        projectView_->Finalize();
#endif
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }

    void ImGuiManager::ApplyCustomTheme()
    {
        Editor::Theme::ApplyImGuiStyle();
    }

    void ImGuiManager::StartNewFrame()
    {

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame(); // ImGuizmoのフレーム開始
    }
}
