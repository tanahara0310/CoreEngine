#include "ImGuiManager.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Utility/Debug/GameDebugUI.h"
#include <ImGuizmo.h>
#include <filesystem>

namespace CoreEngine
{

namespace fs = std::filesystem;

void ImGuiManager::Initialize(HWND hwnd, DirectXCommon* dxCommon)
{

    // クラス情報をメンバ変数に代入
    hwnd_ = hwnd;
    dxCommon_ = dxCommon;

    // SwapChainの情報を取得
    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    dxCommon_->GetSwapChain()->GetDesc(&swapChainDesc);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // キーボードナビゲーションを有効化
    // ドッキング
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // ドッキングを有効化

    io.ConfigWindowsMoveFromTitleBarOnly = true; // タイトルバーからのみウィンドウを移動可能にする

    ImGui::StyleColorsDark();
    ApplyCustomTheme();

    ImFontConfig config = {};
    config.SizePixels = 12.0f;

    const char* fontPath = "C:/Windows/Fonts/YuGothB.ttc";

    if (fs::exists(fontPath)) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            fontPath,
            config.SizePixels,
            &config,
            io.Fonts->GetGlyphRangesJapanese());

        if (font) {
            io.FontDefault = font;
            io.FontGlobalScale = 1.0f;
            io.Fonts->Build();
        }
    } else {
        OutputDebugStringA("フォントファイルが存在しません: YuGothB.ttc\n");
    }

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX12_Init(
        dxCommon_->GetDevice(),
        swapChainDesc.BufferCount,
        dxCommon_->GetRTVDesc().Format,
        dxCommon_->GetSRVHeap(),
        dxCommon_->GetSRVHeap()->GetCPUDescriptorHandleForHeapStart(),
        dxCommon_->GetSRVHeap()->GetGPUDescriptorHandleForHeapStart());

    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(nullptr, nullptr, nullptr);
    ImGui_ImplDX12_CreateDeviceObjects(); // これがないとアクセス違反が起きる

    // SceneViewportの初期化
    sceneViewport_->Initialize();

    // ProjectViewの初期化
    projectView_->Initialize(dxCommon_);
}

void ImGuiManager::Begin(PostEffectManager* postEffectManager, GameDebugUI* gameDebugUI)
{

    // フレームの開始
    StartNewFrame();

    
    // ドッキングUIの開始（メニューバーの高さを考慮してドッキングスペースを配置）
    dockingUI_->BeginDockSpaceHostWindow();
    dockingUI_->SetupDockSpace();

    // シーンビューポートの開始（PostEffectManagerを渡す）
    sceneViewport_->DrawSceneViewport(dxCommon_, postEffectManager);

    // テクスチャビューアの描画（GameDebugUIの状態に応じて）
    bool showTextureViewer = gameDebugUI ? gameDebugUI->IsTextureViewerVisible() : false;
    textureViewer_->DrawTextureViewer(showTextureViewer);

    // プロジェクトビューの更新
    projectView_->Update();

    ImGui::End();
}

void ImGuiManager::End()
{
}

void ImGuiManager::Draw()
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon_->GetCommandList());
}

void ImGuiManager::Finalize()
{
    projectView_->Finalize();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiManager::ApplyCustomTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // ===== より濃いUnity風スタイル設定 =====
    
    // 角丸やサイズ（Unityに近い設定）
    style.WindowRounding = 2.0f;       
    style.FrameRounding = 3.0f;        
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;          
    style.ScrollbarRounding = 2.0f;
    style.ScrollbarSize = 14.0f;       
    
    // パディングとスペーシング（Unity風）
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(4.0f, 3.0f);
    style.ItemSpacing = ImVec2(4.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);

    // ===== 非常に暗いUnity風カラーパレット（プロ仕様） =====
    
    // ベースカラー（Unity Pro版のダークテーマ）
    ImVec4 blackish = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);          // ほぼ黒（最暗BG）
    ImVec4 veryDarkGray = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);      // 非常に濃いグレー（メインBG）
    ImVec4 darkGray = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);          // 濃いグレー（パネル）
    ImVec4 mediumDarkGray = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);    // 中間の暗いグレー（ホバー）
    ImVec4 lightGray = ImVec4(0.28f, 0.28f, 0.28f, 1.0f);         // 明るめグレー（アクティブ）
    
    // Unityの強調色（より鮮やかに）
    ImVec4 unityOrange = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);         // 鮮やかなUnityオレンジ
    ImVec4 unityOrangeHover = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);    // ホバー時のオレンジ
    ImVec4 unityBlue = ImVec4(0.2f, 0.6f, 1.0f, 1.0f);            // Unity風ブルー（情報表示用）
    
    // 重要な要素のアクセント色
    ImVec4 enabledGreen = ImVec4(0.0f, 0.8f, 0.2f, 1.0f);         // 有効状態（緑）
    ImVec4 warningYellow = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);        // 警告（黄色）
    ImVec4 errorRed = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);             // エラー（赤）
    
    // テキストカラー（高コントラスト）
    ImVec4 textBrightWhite = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);   // 非常に明るいメインテキスト
    ImVec4 textLightGray = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);     // サブテキスト
    ImVec4 textMediumGray = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);       // 無効テキスト
    
    // 境界線とセパレーター（さらにシャープに）
    ImVec4 borderVeryDark = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);    // 極暗境界線
    ImVec4 separatorMedium = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);      // セパレーター

    // ===== ImGuiカラー設定（Unity Proライク） =====

    // ウィンドウとパネル（非常にダーク）
    colors[ImGuiCol_WindowBg] = blackish;               // 最も暗い背景
    colors[ImGuiCol_ChildBg] = blackish;                // 子ウィンドウも最暗
    colors[ImGuiCol_PopupBg] = veryDarkGray;            // ポップアップは少し明るく
    colors[ImGuiCol_MenuBarBg] = veryDarkGray;          // メニューバー

    // 枠線（極シャープ）
    colors[ImGuiCol_Border] = borderVeryDark;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // テキスト（最高コントラスト）
    colors[ImGuiCol_Text] = textBrightWhite;
    colors[ImGuiCol_TextDisabled] = textMediumGray;
    colors[ImGuiCol_TextSelectedBg] = unityOrange;

    // タイトルバー（非常にダーク）
    colors[ImGuiCol_TitleBg] = veryDarkGray;
    colors[ImGuiCol_TitleBgActive] = darkGray;
    colors[ImGuiCol_TitleBgCollapsed] = blackish;

    // タブ（Unity風強調）
    colors[ImGuiCol_Tab] = veryDarkGray;                // 非アクティブタブは暗く
    colors[ImGuiCol_TabHovered] = mediumDarkGray;       // ホバー時
    colors[ImGuiCol_TabActive] = unityOrange;           // アクティブタブは鮮やかなオレンジ
    colors[ImGuiCol_TabUnfocused] = blackish;           // フォーカス外
    colors[ImGuiCol_TabUnfocusedActive] = darkGray;     // フォーカス外アクティブ

    // ヘッダー（CollapsibleHeaderなど - より目立つように）
    colors[ImGuiCol_Header] = darkGray;
    colors[ImGuiCol_HeaderHovered] = mediumDarkGray;
    colors[ImGuiCol_HeaderActive] = unityOrange;

    // フレーム（入力欄など）
    colors[ImGuiCol_FrameBg] = veryDarkGray;            // より暗い入力欄
    colors[ImGuiCol_FrameBgHovered] = darkGray;         // ホバー時
    colors[ImGuiCol_FrameBgActive] = mediumDarkGray;    // アクティブ時

    // ボタン（重要な要素として強調）
    colors[ImGuiCol_Button] = darkGray;                 // 通常ボタン
    colors[ImGuiCol_ButtonHovered] = lightGray;         // ホバー時はより明るく
    colors[ImGuiCol_ButtonActive] = unityOrange;        // クリック時は鮮やかなオレンジ

    // チェックボックス・ラジオボタン（有効時は緑、通常時はオレンジ）
    colors[ImGuiCol_CheckMark] = enabledGreen;          // チェックマークは緑

    // スライダー（重要な操作として強調）
    colors[ImGuiCol_SliderGrab] = unityOrange;          // スライダーハンドル
    colors[ImGuiCol_SliderGrabActive] = unityOrangeHover; // アクティブ時

    // スクロールバー（非常にダーク）
    colors[ImGuiCol_ScrollbarBg] = blackish;
    colors[ImGuiCol_ScrollbarGrab] = veryDarkGray;
    colors[ImGuiCol_ScrollbarGrabHovered] = darkGray;
    colors[ImGuiCol_ScrollbarGrabActive] = unityOrange;

    // セパレーター（はっきりと見えるように）
    colors[ImGuiCol_Separator] = separatorMedium;
    colors[ImGuiCol_SeparatorHovered] = unityOrange;
    colors[ImGuiCol_SeparatorActive] = unityOrangeHover;

    // リサイズグリップ
    colors[ImGuiCol_ResizeGrip] = veryDarkGray;
    colors[ImGuiCol_ResizeGripHovered] = darkGray;
    colors[ImGuiCol_ResizeGripActive] = unityOrange;

    // プロットとプログレスバー（状態に応じて色分け）
    colors[ImGuiCol_PlotLines] = unityBlue;             // ライン系は青
    colors[ImGuiCol_PlotLinesHovered] = unityOrange;
    colors[ImGuiCol_PlotHistogram] = enabledGreen;      // ヒストグラムは緑
    colors[ImGuiCol_PlotHistogramHovered] = unityOrangeHover;

    // テーブル（非常にダーク）
    colors[ImGuiCol_TableHeaderBg] = veryDarkGray;
    colors[ImGuiCol_TableBorderStrong] = borderVeryDark;
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

    // ドラッグ＆ドロップ
    colors[ImGuiCol_DragDropTarget] = unityOrange;

    // ナビゲーション
    colors[ImGuiCol_NavHighlight] = unityOrange;
    colors[ImGuiCol_NavWindowingHighlight] = unityOrangeHover;
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);

    // モーダルウィンドウ背景
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);

    // ドッキング関連（非常にダーク）
    colors[ImGuiCol_DockingPreview] = unityOrange;
    colors[ImGuiCol_DockingEmptyBg] = blackish;

    // フレーム枠線（Unity風に明確に）
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    
    // より良い視認性のため、コントラストを強化
    style.Alpha = 1.0f;  // 完全に不透明に
}

void ImGuiManager::StartNewFrame()
{

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame(); // ImGuizmoのフレーム開始
}
}
