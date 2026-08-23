#include "pch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "ImGuiManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/SwapChain/SwapChain.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/Render.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "Utility/Debug/GameDebugUI.h"
#include "WinApp/WinApp.h"
#include <ImGuizmo.h>
#include <filesystem>

namespace CoreEngine
{

    namespace fs = std::filesystem;

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

        // imgui.ini の保存先を Cache フォルダに変更
        std::filesystem::create_directories("Cache");
        static const std::string kIniPath = "Cache/imgui.ini";
        io.IniFilename = kIniPath.c_str();

        ImGui::StyleColorsDark();
        ApplyCustomTheme();

        // 副ウィンドウは OS ウィンドウとして描かれるため、角丸と半透明背景は見た目が崩れる
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // DPIスケールを取得してフォントサイズに反映
        float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        float baseFontSize = 12.0f;

        ImFontConfig config = {};
        config.SizePixels = baseFontSize * dpiScale;

        // 日本語グリフ範囲に加え、罫線文字・記号を追加
        static const ImWchar kExtraRanges[] = {
            0x2022, 0x2022, // Bullet •
            0x2500, 0x257F, // Box Drawing 
            0x2580, 0x259F, // Block Elements
            0x25A0, 0x25FF, // Geometric Shapes
            0x2713, 0x2713, // Check mark 
            0,
        };

        ImFontGlyphRangesBuilder rangesBuilder;
        rangesBuilder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        rangesBuilder.AddRanges(kExtraRanges);
        static ImVector<ImWchar> fontRanges;
        fontRanges.clear();
        rangesBuilder.BuildRanges(&fontRanges);

        const char* fontPath = "C:/Windows/Fonts/YuGothB.ttc";

        if (fs::exists(fontPath)) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                fontPath,
                config.SizePixels,
                &config,
                fontRanges.Data);

            if (font) {
                io.FontDefault = font;
                io.FontGlobalScale = 1.0f;
                io.Fonts->Build();
            }
        } else {
            OutputDebugStringA("フォントファイルが存在しません: YuGothB.ttc\n");
        }

        ImGui_ImplWin32_Init(hwnd_);

        // ImGui のフォント用スロットは DescriptorAllocator から正規に確保する。
        // 旧実装はヒープ先頭（slot 0）を直接使い、DescriptorAllocator 側は
        // kUserSRVStart=1 という定数で「slot 0 は ImGui のもの」と暗黙に予約していた。
        // 予約定数を廃止した際、この暗黙の前提だけが残って深度 SRV と衝突し、
        // フォント生成が深度 SRV を上書きして描画が壊れた。
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

#ifdef USE_IMGUI
        // ProjectViewの初期化
        projectView_->Initialize(dxCommon_);
#endif
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
        // レイアウト構築は BeginDockSpaceHostWindow が DockSpace 提出前に内部で行う
        dockingUI_->BeginDockSpaceHostWindow();

        // Game ビューポートは PostEffectPass 完了後に別経路で描画する。
#ifdef USE_IMGUI
        // Canvas プレビューウィンドウ（UI のみを表示）
        canvasViewport_->DrawCanvasViewport();


        // プロジェクトビューの更新

        projectView_->Update();
#endif

        ImGui::End();
    }

    void ImGuiManager::DrawGameViewport([[maybe_unused]] GraphicsCore* dxCommon, [[maybe_unused]] PostEffectManager* postEffectManager, [[maybe_unused]] GameDebugUI* gameDebugUI)
    {
#ifdef USE_IMGUI
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
        const float aspect = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
            static_cast<float>(WinApp::GetCurrentClientHeightStatic());

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

        // Gameビュー上のギズモ・オブジェクト選択・モデルドロップ。
        // エディタ機能の有効条件は USE_IMGUI（Development ビルドにも必要）。_DEBUG で囲まないこと
        SceneDebugEditor* sceneDebugEditor = gameDebugUI ? gameDebugUI->GetSceneDebugEditor() : nullptr;

        if (sceneDebugEditor) {
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            const ImVec2 imageMax = ImGui::GetItemRectMax();
            const ImVec2 imageSize(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
            sceneDebugEditor->AcceptGameViewportModelDrop(imageMin, imageSize);
            const bool isImageHovered = ImGui::IsItemHovered();
            sceneDebugEditor->UpdateGameViewportInteraction(
                imageMin,
                imageSize,
                isImageHovered);
        }

        ImGui::End();
#endif
    }

    void ImGuiManager::DrawFullscreenGameViewport([[maybe_unused]] D3D12_GPU_DESCRIPTOR_HANDLE textureHandle)
    {
#ifdef USE_IMGUI
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
            const float aspect = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
                static_cast<float>(WinApp::GetCurrentClientHeightStatic());

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
#ifdef USE_IMGUI
        projectView_->Finalize();
#endif
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiManager::ApplyCustomTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // ===== 配色 =====
        // 方針: ①背景に明度の段差を作る ②アクセント色は「選択・操作中」だけに使う
        //       ③面の区切りは枠線ではなく明度差で行う（FrameBorderSize = 0）
        //
        // 【重要】ImGui は sRGB の RTV へ描くので、リニア値を渡すと書き込み時に持ち上がる。
        // 配色は「画面に出したい sRGB の 0-255 値」で書き、下の srgb ヘルパでリニアへ逆変換する。
        const auto srgb = [](int r, int g, int b, float a = 1.0f) -> ImVec4 {
            const auto toLinear = [](int v8) {
                const float c = static_cast<float>(v8) / 255.0f;
                return (c <= 0.04045f) ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
            };
            return ImVec4(toLinear(r), toLinear(g), toLinear(b), a);
        };

        // 背景の階調（暗 → 明）。Unity ダークテーマの実測値に近い並び
        const ImVec4 bgDeepest = srgb(20, 20, 21);    // 最奥（スクロールバー溝など）
        const ImVec4 bgField = srgb(26, 26, 28);    // 入力欄（一段沈める）
        const ImVec4 bgChild = srgb(30, 30, 33);    // 子パネル（少し沈める）
        const ImVec4 bgWindow = srgb(36, 36, 40);    // ウィンドウ
        const ImVec4 bgPanel = srgb(44, 44, 48);    // メニューバー・タイトル・ポップアップ
        const ImVec4 bgControl = srgb(58, 58, 64);    // ボタン・タブ選択
        const ImVec4 bgHover = srgb(74, 74, 82);    // ホバー
        const ImVec4 bgActive = srgb(90, 90, 100);   // 押下

        // アクセント（青。選択・操作中の要素だけに使う）
        const ImVec4 accent = srgb(61, 126, 200);
        const ImVec4 accentHover = srgb(85, 150, 222);
        const ImVec4 accentMuted = srgb(42, 63, 85);   // 選択行の下地
        const ImVec4 accentWarm = srgb(242, 156, 49); // 差し色（少量だけ使う）

        // テキスト
        const ImVec4 textPrimary = srgb(238, 238, 242);
        const ImVec4 textDisabled = srgb(128, 128, 138);

        const ImVec4 borderColor = srgb(15, 15, 16);
        const ImVec4 transparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // ===== 面 =====
        colors[ImGuiCol_WindowBg] = bgWindow;
        colors[ImGuiCol_ChildBg] = bgChild;
        colors[ImGuiCol_PopupBg] = bgPanel;
        colors[ImGuiCol_MenuBarBg] = bgPanel;
        colors[ImGuiCol_Border] = borderColor;
        colors[ImGuiCol_BorderShadow] = transparent;

        // ===== 文字 =====
        colors[ImGuiCol_Text] = textPrimary;
        colors[ImGuiCol_TextDisabled] = textDisabled;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        colors[ImGuiCol_TextLink] = accentHover;
        colors[ImGuiCol_InputTextCursor] = textPrimary;

        // ===== タイトルバー =====
        colors[ImGuiCol_TitleBg] = bgPanel;
        colors[ImGuiCol_TitleBgActive] = bgPanel;
        colors[ImGuiCol_TitleBgCollapsed] = bgWindow;

        // ===== タブ =====
        // 選択タブは「明るい面 ＋ 上端の細いアクセント線」で示す。
        // 面全体をアクセント色で塗り潰さないのが現代的なエディタの作法。
        colors[ImGuiCol_Tab] = bgPanel;
        colors[ImGuiCol_TabHovered] = bgHover;
        colors[ImGuiCol_TabSelected] = bgControl;
        colors[ImGuiCol_TabSelectedOverline] = accentHover;
        colors[ImGuiCol_TabDimmed] = bgChild;
        colors[ImGuiCol_TabDimmedSelected] = bgPanel;
        colors[ImGuiCol_TabDimmedSelectedOverline] = accentMuted;

        // ===== ヘッダ（Selectable / TreeNode / CollapsingHeader 共用） =====
        // 選択行としても折りたたみ見出しとしても破綻しないよう、
        // 彩度を落とした青灰色を使う。
        colors[ImGuiCol_Header] = accentMuted;
        colors[ImGuiCol_HeaderHovered] = bgHover;
        colors[ImGuiCol_HeaderActive] = accent;

        // ===== 入力欄 =====
        colors[ImGuiCol_FrameBg] = bgField;
        colors[ImGuiCol_FrameBgHovered] = srgb(32, 32, 36);
        colors[ImGuiCol_FrameBgActive] = srgb(38, 38, 43);

        // ===== ボタン =====
        colors[ImGuiCol_Button] = bgControl;
        colors[ImGuiCol_ButtonHovered] = bgHover;
        colors[ImGuiCol_ButtonActive] = bgActive;

        // ===== 操作子 =====
        colors[ImGuiCol_CheckMark] = accentHover;
        colors[ImGuiCol_SliderGrab] = accent;
        colors[ImGuiCol_SliderGrabActive] = accentHover;

        colors[ImGuiCol_ScrollbarBg] = bgDeepest;
        colors[ImGuiCol_ScrollbarGrab] = srgb(63, 63, 70);
        colors[ImGuiCol_ScrollbarGrabHovered] = bgHover;
        colors[ImGuiCol_ScrollbarGrabActive] = bgActive;

        colors[ImGuiCol_Separator] = srgb(56, 56, 62);
        colors[ImGuiCol_SeparatorHovered] = accent;
        colors[ImGuiCol_SeparatorActive] = accentHover;

        // リサイズグリップは掴むまで見せない
        colors[ImGuiCol_ResizeGrip] = transparent;
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        colors[ImGuiCol_ResizeGripActive] = accent;

        // ===== グラフ =====
        colors[ImGuiCol_PlotLines] = accentHover;
        colors[ImGuiCol_PlotLinesHovered] = accentWarm;
        colors[ImGuiCol_PlotHistogram] = accentWarm;
        colors[ImGuiCol_PlotHistogramHovered] = srgb(255, 204, 120);

        // ===== テーブル =====
        colors[ImGuiCol_TableHeaderBg] = bgPanel;
        colors[ImGuiCol_TableBorderStrong] = srgb(60, 60, 66);
        colors[ImGuiCol_TableBorderLight] = srgb(42, 42, 47);
        colors[ImGuiCol_TableRowBg] = transparent;
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.022f);

        // ===== その他 =====
        colors[ImGuiCol_DragDropTarget] = accentWarm;
        colors[ImGuiCol_NavCursor] = accentHover;
        colors[ImGuiCol_NavWindowingHighlight] = accentHover;
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);
        colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        colors[ImGuiCol_DockingEmptyBg] = bgDeepest;

        // ===== 形状・余白 =====
        // 余白を広げるのが「安っぽさ」を消す一番効く調整。
        // 以前は FramePadding(4,3) / ItemSpacing(4,4) で要素が詰まりすぎていた。
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;

        style.WindowRounding = 4.0f;
        style.ChildRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 4.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 4.0f;
        style.ScrollbarRounding = 6.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;  // 全ウィジェットの黒枠をやめる
        style.TabBarBorderSize = 1.0f;
        style.TabBarOverlineSize = 2.0f;
        style.DockingSeparatorSize = 2.0f;

        // 見出し（SeparatorText）を左寄せの太線にして、セクションの区切りを読みやすくする
        style.SeparatorTextBorderSize = 2.0f;
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(16.0f, 6.0f);

        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None; // タイトル左の折りたたみ矢印を消す

        style.Alpha = 1.0f;
    }

    void ImGuiManager::StartNewFrame()
    {

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame(); // ImGuizmoのフレーム開始
    }
}
